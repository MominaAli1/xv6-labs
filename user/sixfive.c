#include "kernel/types.h"
#include "user/user.h"

char *seps = " -\r\t\n./,";

void
sixfive(int fd)
{
char c;
int value = 0, hasdigit = 0, valid = 1;
while(1){
int atend = (read(fd, &c, 1) != 1);
if(atend || strchr(seps, c)){
if(valid && hasdigit && (value % 5 == 0 || value % 6 == 0))
printf("%d\n", value);
value = 0; hasdigit = 0; valid = 1;
if(atend)
break;
} else if(c >= '0' && c <= '9'){
if(valid)
value = value * 10 + (c - '0');
hasdigit = 1;
} else {
valid = 0;
}
}
}

int
main(int argc, char *argv[])
{
if(argc <= 1){
sixfive(0);
exit(0);
}
for(int i = 1; i < argc; i++){
int fd = open(argv[i], 0);
if(fd < 0){
fprintf(2, "sixfive: cannot open %s\n", argv[i]);
continue;
}
sixfive(fd);
close(fd);
}
exit(0);
}
