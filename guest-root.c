#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include "guest-fd-hygiene.h"
/* The preflight makes its whole namespace private, then shares only its own
 * tmpfs /mnt. Preserve that peer group so vold's FUSE mounts reach zygote.
 * Refuse a caller with any other shared mount, especially stock /mnt. */
static int check_shared_mounts(const char *root) {
 FILE *f=fopen("/proc/self/mountinfo","re");
 if(!f)return -1;
 char *line=NULL;size_t cap=0;int ok=1;
 char allowed[256];snprintf(allowed,sizeof(allowed),"%s/mnt",root);
 while(getline(&line,&cap,f)>0) {
  char path[4096];
  if(sscanf(line,"%*s %*s %*s %*s %4095s",path)!=1){ok=0;break;}
  char *sep=strstr(line," - ");if(!sep){ok=0;break;}*sep=0;
  if(strstr(line," shared:") &&
     !(strncmp(path,allowed,strlen(allowed))==0 &&
       (path[strlen(allowed)]==0 || path[strlen(allowed)]=='/'))) {
   fprintf(stderr,"unexpected shared mount: %s\n",path);ok=0;break;
  }
 }
 if(ferror(f))ok=0;
 free(line);fclose(f);return ok?0:-1;
}
int main(int argc,char **argv) {
 if(argc<3 || strcmp(argv[1],"/data/local/tmp/gsi17-graphics-20261105-01/rootfs"))return 2;
 if(guest_close_inherited_fds()){perror("close inherited guest fds");return 1;}
 if(check_shared_mounts(argv[1]))return 1;
 if(unshare(CLONE_NEWNS)||mount(NULL,"/",NULL,MS_PRIVATE,NULL)||chdir(argv[1])){perror("isolate");return 1;}
 if(syscall(SYS_pivot_root,".","postinstall")||chdir("/")||umount2("/postinstall",MNT_DETACH)){perror("pivot");return 1;}
 pid_t child=fork();
 if(child<0){perror("fork");return 1;}
 if(!child){execv(argv[2],argv+2);perror("exec");_exit(127);}
 fprintf(stderr,"guest child pid=%d\n",child);
 int status; while(waitpid(child,&status,0)<0){if(errno!=EINTR)return 1;}
 return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
}
