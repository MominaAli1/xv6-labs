//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "memlayout.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if (pfd)
    *pfd = fd;
  if (pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for (fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd] == 0) {
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if (argfd(0, 0, &f) < 0)
    return -1;
  if ((fd = fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if (argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((ip = namei(old)) == 0) {
    end_op();
    return -1;
  }

  ilock(ip);
  if (ip->type == T_DIR) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  if (ip->nlink >= NLINK_MAX) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if ((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  // dp may have been unlinked while we resolved it; linking into an
  // orphaned directory leaks ip (itrunc discards the record without
  // dropping ip->nlink).  create() has the same guard.
  if (dp->nlink == 0) {
    iunlockput(dp);
    goto bad;
  }
  if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0) {
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for (off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if (de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((dp = nameiparent(path, name)) == 0) {
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if ((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if (ip->nlink < 1)
    panic("unlink: nlink < 1");
  if (ip->type == T_DIR && !isdirempty(ip)) {
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if (ip->type == T_DIR) {
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode *
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if ((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if (dp->nlink == 0) {
    iunlockput(dp);
    return 0;
  }

  // a new directory's ".." would push dp->nlink past its maximum
  if (type == T_DIR && dp->nlink >= NLINK_MAX) {
    iunlockput(dp);
    return 0;
  }

  if ((ip = dirlookup(dp, name, 0)) != 0) {
    iunlockput(dp);
    ilock(ip);
    if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if ((ip = ialloc(dp->dev, type)) == 0) {
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if (type == T_DIR) { // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if (dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if (type == T_DIR) {
    // now that success is guaranteed:
    dp->nlink++; // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if ((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if (omode & O_CREATE) {
    ip = create(path, T_FILE, 0, 0);
    if (ip == 0) {
      end_op();
      return -1;
    }
  } else {
    if ((ip = namei(path)) == 0) {
      end_op();
      return -1;
    }
    ilock(ip);
    if (ip->type == T_DIR && omode != O_RDONLY) {
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0) {
    if (f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if (ip->type == T_DEVICE) {
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if ((omode & O_TRUNC) && ip->type == T_FILE) {
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0) {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if ((argstr(0, path, MAXPATH)) < 0 ||
      (ip = create(path, T_DEVICE, major, minor)) == 0) {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0) {
    end_op();
    return -1;
  }
  ilock(ip);
  if (ip->type != T_DIR) {
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if (argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for (i = 0;; i++) {
    if (i >= NELEM(argv)) {
      goto bad;
    }
    if (fetchaddr(uargv + sizeof(uint64) * i, (uint64 *)&uarg) < 0) {
      goto bad;
    }
    if (uarg == 0) {
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if (argv[i] == 0)
      goto bad;
    if (fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = kexec(path, argv);

  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

bad:
  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if (pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0) {
    if (fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if (copyout(p->pagetable, p->sz, fdarray, (char *)&fd0, sizeof(fd0)) < 0 ||
      copyout(p->pagetable, p->sz, fdarray + sizeof(fd0), (char *)&fd1,
              sizeof(fd1)) < 0) {
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

//
// mmap and munmap.
//

// the mmap-ed region that contains address va, or 0.
static struct vma *
vmafind(struct proc *p, uint64 va)
{
  for (int i = 0; i < NVMA; i++) {
    struct vma *v = &p->vmas[i];
    if (v->used && va >= v->addr && va < v->addr + v->len)
      return v;
  }
  return 0;
}

uint64
sys_mmap(void)
{
  uint64 len, off;
  int prot, flags;
  struct file *f;
  struct proc *p = myproc();

  // argument 0 (addr) is always 0: the kernel picks the address.
  argaddr(1, &len);
  argint(2, &prot);
  argint(3, &flags);
  argaddr(5, &off);
  if (argfd(4, 0, &f) < 0)
    return -1;

  if (len == 0 || off % PGSIZE != 0 || f->type != FD_INODE)
    return -1;
  if (flags != MAP_SHARED && flags != MAP_PRIVATE)
    return -1;
  if ((prot & PROT_READ) && !f->readable)
    return -1;
  // a shared writable mapping writes to the file, so the file must be
  // open for writing. a private one is never written back.
  if ((prot & PROT_WRITE) && flags == MAP_SHARED && !f->writable)
    return -1;

  // put the new region just below the lowest existing one
  // (or below the trapframe if there are none).
  struct vma *v = 0;
  uint64 top = TRAPFRAME;
  for (int i = 0; i < NVMA; i++) {
    if (p->vmas[i].used) {
      if (p->vmas[i].addr < top)
        top = p->vmas[i].addr;
    } else if (v == 0) {
      v = &p->vmas[i];
    }
  }
  len = PGROUNDUP(len);
  if (v == 0 || len == 0 || top < len || top - len < PGROUNDUP(p->sz))
    return -1;

  // lazy: don't allocate memory or read the file now.
  // mmapfault() does that when a page is first used.
  v->used = 1;
  v->addr = top - len;
  v->len = len;
  v->prot = prot;
  v->flags = flags;
  v->off = off;
  v->f = filedup(f);
  return v->addr;
}

// write page va (physical page pa) of MAP_SHARED region v back
// to the file, without growing the file.
static void
vmawriteback(struct vma *v, uint64 va, uint64 pa)
{
  struct inode *ip = v->f->ip;
  uint off = v->off + (va - v->addr);
  int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE; // as in filewrite()
  int done = 0;

  while (done < PGSIZE) {
    int n = 0;
    begin_op();
    ilock(ip);
    if (off + done < ip->size) {
      n = ip->size - (off + done);
      if (n > PGSIZE - done)
        n = PGSIZE - done;
      if (n > max)
        n = max;
      writei(ip, 0, pa + done, off + done, n);
    }
    iunlock(ip);
    end_op();
    if (n <= 0)
      break;
    done += n;
  }
}

// unmap [addr, addr+len) of region v, writing back pages of a
// MAP_SHARED region that were modified (PTE_D). the range is at
// the start or the end of v.
static void
vmaunmap(struct vma *v, pagetable_t pagetable, uint64 addr, uint64 len)
{
  for (uint64 va = addr; va < addr + len; va += PGSIZE) {
    pte_t *pte = walk(pagetable, va, 0);
    if (pte == 0 || (*pte & PTE_V) == 0)
      continue; // never used, so never mapped
    if ((v->flags & MAP_SHARED) && (*pte & PTE_D))
      vmawriteback(v, va, PTE2PA(*pte));
    uvmunmap(pagetable, va, 1, 1);
  }

  if (addr == v->addr) {
    v->addr += len;
    v->off += len;
  }
  v->len -= len;
  if (v->len == 0) {
    fileclose(v->f);
    v->used = 0;
  }
}

uint64
sys_munmap(void)
{
  uint64 addr, len;
  struct proc *p = myproc();

  argaddr(0, &addr);
  argaddr(1, &len);
  if (addr % PGSIZE != 0 || len == 0)
    return -1;
  len = PGROUNDUP(len);

  struct vma *v = vmafind(p, addr);
  if (v == 0 || addr + len > v->addr + v->len)
    return -1;
  // only unmapping at the start or the end of a region is supported.
  if (addr != v->addr && addr + len != v->addr + v->len)
    return -1;

  vmaunmap(v, p->pagetable, addr, len);
  return 0;
}

// unmap all of p's regions (called by exit and exec).
void
munmapall(struct proc *p, pagetable_t pagetable)
{
  for (int i = 0; i < NVMA; i++) {
    struct vma *v = &p->vmas[i];
    if (v->used)
      vmaunmap(v, pagetable, v->addr, v->len);
  }
}

// page fault at va. if va is in an mmap-ed region and the access is
// allowed, read that page of the file into a new page and map it.
// returns the physical address, or 0 (the process is then killed).
uint64
mmapfault(struct proc *p, uint64 va, int read)
{
  struct vma *v = vmafind(p, va);
  if (v == 0)
    return 0;
  if (read && !(v->prot & PROT_READ))
    return 0;
  if (!read && !(v->prot & PROT_WRITE))
    return 0; // e.g. a write to a PROT_READ mapping

  va = PGROUNDDOWN(va);
  if (ismapped(p->pagetable, va))
    return 0;

  char *mem = kalloc();
  if (mem == 0)
    return 0;
  memset(mem, 0, PGSIZE); // bytes past the end of the file read as 0

  struct inode *ip = v->f->ip;
  ilock(ip);
  readi(ip, 0, (uint64)mem, v->off + (va - v->addr), PGSIZE);
  iunlock(ip);

  int perm = PTE_U | PTE_R;
  if (v->prot & PROT_WRITE)
    perm |= PTE_W;
  if (v->prot & PROT_EXEC)
    perm |= PTE_X;
  if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, perm) != 0) {
    kfree(mem);
    return 0;
  }
  return (uint64)mem;
}
