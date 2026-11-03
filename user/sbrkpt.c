#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/riscv.h"
#include "user/user.h"

//
// Exercise: grow the address space by one byte with sbrk(1),
// and compare the page table before and after.
//

// print the PTE for the page that contains va.
static void
show(char *label, char *va)
{
  uint64 pte = pgpte(va);

  printf("%s: va %p pte 0x%lx pa 0x%lx flags 0x%lx", label, va, pte,
         PTE2PA(pte), PTE_FLAGS(pte));
  if (pte == 0) {
    printf(" (not mapped)\n");
    return;
  }
  printf(" [%s%s%s%s%s%s%s]\n", (pte & PTE_V) ? "V" : "",
         (pte & PTE_R) ? "R" : "", (pte & PTE_W) ? "W" : "",
         (pte & PTE_X) ? "X" : "", (pte & PTE_U) ? "U" : "",
         (pte & (1L << 6)) ? "A" : "", (pte & (1L << 7)) ? "D" : "");
}

int
main(int argc, char *argv[])
{
  char *before = sbrk(0);
  printf("size before sbrk(1): %p\n", before);
  printf("--- page table BEFORE sbrk(1) ---\n");
  vmprint();
  show("page of the new byte, before", before);

  char *p = sbrk(1);
  char *after = sbrk(0);
  printf("\nsbrk(1) returned %p, size is now %p (grew by %d byte)\n", p, after,
         (int)(after - before));
  printf("--- page table AFTER sbrk(1) ---\n");
  vmprint();
  show("page of the new byte, after ", p);

  *p = 'x'; // use the new byte
  show("after writing the new byte  ", p);

  // a second sbrk(1) lands in the same page: no new memory.
  char *q = sbrk(1);
  show("second sbrk(1), same page   ", q);

  // for comparison: lazy sbrk only raises the size; the page is
  // allocated by the page-fault handler on first use.
  char *l = sbrklazy(PGSIZE);
  char *lpage = (char *)PGROUNDUP((uint64)l);
  show("\nlazy sbrk, before touching  ", lpage);
  *lpage = 'y';
  show("lazy sbrk, after touching   ", lpage);

  exit(0);
}
