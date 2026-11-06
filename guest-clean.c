#define _GNU_SOURCE
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
 if(argc!=3 || strcmp(argv[1],"/data/local/tmp/gsi17-graphics-20261105-01/rootfs"))return 2;
 struct stat root,host,ns,one;
 if(stat(argv[1],&root)||stat("/",&host)||stat("/proc/1/ns/mnt",&one))return 3;
 if(root.st_dev==host.st_dev&&root.st_ino==host.st_ino)return 4;
 DIR *d=opendir("/proc"); if(!d)return 5;
 struct dirent *e; char p[128];
 while((e=readdir(d))) {
  char *end; long pid=strtol(e->d_name,&end,10);
  if(*end||pid<=1||pid==getpid())continue;
  snprintf(p,sizeof(p),"/proc/%ld/root",pid);
  if(stat(p,&ns)||ns.st_dev!=root.st_dev||ns.st_ino!=root.st_ino)continue;
  snprintf(p,sizeof(p),"/proc/%ld/ns/mnt",pid);
  if(stat(p,&ns)||(ns.st_dev==one.st_dev&&ns.st_ino==one.st_ino))continue;
  printf("guest pid=%ld\n",pid);
  if(!strcmp(argv[2],"kill"))kill((pid_t)pid,SIGKILL);
 }
 closedir(d);return 0;
}
