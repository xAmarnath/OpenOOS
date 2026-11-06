#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
struct binderfs_device { char name[256]; uint32_t major,minor; };
#define BINDER_CTL_ADD _IOWR('b',1,struct binderfs_device)
int main(int argc,char **argv) {
 if(argc!=3)return 2;
 int fd=open(argv[1],O_RDONLY|O_CLOEXEC);if(fd<0){perror("control");return 1;}
 struct binderfs_device d={0};snprintf(d.name,sizeof(d.name),"%s",argv[2]);
 if(ioctl(fd,BINDER_CTL_ADD,&d)){perror("binder add");return 1;}
 printf("%s %u:%u\n",d.name,d.major,d.minor);close(fd);return 0;
}
