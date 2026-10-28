#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  char *data = sbrk(64 * 4096);
  char *marker = "Here it is: ";
  int mlen = strlen(marker);
  for (int i = 0; i < 64 * 4096 - mlen; i++) {
    int k = 0;
    while (k < mlen && data[i + k] == marker[k]) k++;
    if (k < mlen) continue;
    char *s = data + i + mlen;
    if (!((s[0]>='0'&&s[0]<='9')||(s[0]>='a'&&s[0]<='z')||(s[0]>='A'&&s[0]<='Z'))) continue;
    for (int j = 0; (s[j]>='0'&&s[j]<='9')||(s[j]>='a'&&s[j]<='z')||(s[j]>='A'&&s[j]<='Z'); j++)
      printf("%c", s[j]);
    printf("\n");
    exit(0);
  }
  exit(1);
}
