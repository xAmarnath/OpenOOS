#define _GNU_SOURCE
#include "guest-fd-hygiene.h"
#include <stdio.h>
int main(int argc,char **argv) {
    if(argc<2 || guest_close_inherited_fds())return 2;
    execv(argv[1],argv+1);perror("exec");return 1;
}
