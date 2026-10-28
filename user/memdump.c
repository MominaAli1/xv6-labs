#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void memdump(char *fmt, char *data, int len);

int
main(int argc, char *argv[])
{
  if (argc == 1) {
    printf("Example 1:\n");
    int a[2] = {61810, 2026};
    memdump("ii", (char *)a, sizeof(a));

    printf("Example 2:\n");
    memdump("S", "a string", sizeof("a string"));

    printf("Example 3:\n");
    char *s = "another";
    memdump("s", (char *)&s, sizeof(s));

    struct sss {
      char *ptr;
      int num1;
      short num2;
      char byte;
      char bytes[8];
    } example;

    example.ptr = "hello";
    example.num1 = 1819438967;
    example.num2 = 100;
    example.byte = 'z';
    strcpy(example.bytes, "xyzzy");

    printf("Example 4:\n");
    memdump("pihcS", (char *)&example, sizeof(example));

    printf("Example 5:\n");
    memdump("sccccc", (char *)&example, sizeof(example));
  } else if (argc == 2) {
    // format in argv[1], up to 512 bytes of data from standard input.
    char data[512];
    int n = 0;
    memset(data, '\0', sizeof(data));
    while (n < sizeof(data)) {
      int nn = read(0, data + n, sizeof(data) - n);
      if (nn <= 0)
        break;
      n += nn;
    }
    memdump(argv[1], data, n);
  } else {
    printf("Usage: memdump [format]\n");
    exit(1);
  }
  exit(0);
}

  // Your code here.  `data` holds `len` valid bytes.
void
memdump(char *fmt, char *data, int len)
{
  int off = 0;
  for(int f = 0; fmt[f]; f++){
    char t = fmt[f];
    int need;
    if(t == 'i') need = 4;
    else if(t == 'p') need = 8;
    else if(t == 's') need = 8;
    else if(t == 'h') need = 2;
    else if(t == 'c') need = 1;
    else if(t == 'S') need = 0;
    else continue;

    if(t != 'S' && off + need > len){
      printf("memdump: not enough data for '%c'\n", t);
      return;
    }

    if(t == 'i'){
      uint x = 0;
      for(int k = 0; k < 4; k++)
        x |= (uint)(uchar)data[off+k] << (8*k);
      printf("%d\n", (int)x);
      off += 4;
    } else if(t == 'h'){
      ushort x = 0;
      for(int k = 0; k < 2; k++)
        x |= (ushort)((uchar)data[off+k]) << (8*k);
      printf("%d\n", (int)x);
      off += 2;
    } else if(t == 'c'){
      printf("%c\n", data[off]);
      off += 1;
    } else if(t == 'p'){
      uint64 x = 0;
      for(int k = 0; k < 8; k++)
        x |= (uint64)(uchar)data[off+k] << (8*k);
      printf("%lx\n", x);
      off += 8;
    } else if(t == 's'){
      uint64 x = 0;
      for(int k = 0; k < 8; k++)
        x |= (uint64)(uchar)data[off+k] << (8*k);
      printf("%s\n", (char*)x);
      off += 8;
    } else if(t == 'S'){
      for(; off < len && data[off] != 0; off++)
        printf("%c", data[off]);
      printf("\n");
      off = len;
    }
  }
}
