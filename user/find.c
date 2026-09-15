#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/param.h"
#include "user/user.h"

void
run(char **cmd, char *file)
{
  char *args[MAXARG];
  int i = 0;
  for(; cmd[i]; i++)
    args[i] = cmd[i];
  args[i++] = file;
  args[i] = 0;

  if(fork() == 0){
    exec(args[0], args);
    fprintf(2, "find: exec %s failed\n", args[0]);
    exit(1);
  }
  wait(0);
}

void
find(char *path, char *name, char **cmd)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }
  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  for(p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;
  if(strcmp(p, name) == 0){
    if(cmd)
      run(cmd, path);
    else
      printf("%s\n", path);
  }

  if(st.type == T_DIR){
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      fprintf(2, "find: path too long\n");
      close(fd);
      return;
    }
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      find(buf, name, cmd);
    }
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc < 3){
    fprintf(2, "usage: find path name [-exec cmd args...]\n");
    exit(1);
  }

  char **cmd = 0;
  if(argc >= 5 && strcmp(argv[3], "-exec") == 0)
    cmd = &argv[4];

  find(argv[1], argv[2], cmd);
  exit(0);
}
