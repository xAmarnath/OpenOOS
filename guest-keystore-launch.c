#define _GNU_SOURCE
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/xattr.h>

static volatile int *diagnostic;
static void die(const char *s) { int saved=errno; if(diagnostic)diagnostic[1]=saved; errno=saved; perror(s); exit(1); }
int main(int argc, char **argv) {
    struct stat marker, guest, host, gb, hb;
    if (stat("/data/local/tmp/guest-runtime-only", &marker) ||
        stat("/data", &guest) || stat("/proc/1/root/data", &host) ||
        stat("/dev/binderfs/binder", &gb) ||
        stat("/proc/1/root/dev/binderfs/binder", &hb)) die("guest check");
    if (guest.st_dev == host.st_dev || gb.st_rdev == hb.st_rdev) {
        fputs("refusing shared stock data or Binder device\n", stderr); return 2;
    }
    if (argc != 1 && !(argc == 2 && !strcmp(argv[1], "--probe"))) return 2;
    {
        diagnostic=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
        if(diagnostic==MAP_FAILED) {diagnostic=NULL;die("diagnostic mmap");}
        pid_t child=fork();if(child<0)die("fork");
        if(child) {
            int status;while(waitpid(child,&status,0)<0)if(errno!=EINTR)die("wait");
            fprintf(stderr,"probe stage=%d errno=%d (%s) exit=%d signal=%d\n",diagnostic[0],diagnostic[1],strerror(diagnostic[1]),WIFEXITED(status)?WEXITSTATUS(status):-1,WIFSIGNALED(status)?WTERMSIG(status):0);
            return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
        }
        diagnostic[0]=1;
    }
    fputs("guest isolation verified; setting keystore identity\n", stderr);
    int output = open("/data/misc/keystore/launcher.log", O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC, 0600);
    const char *label = "u:object_r:keystore_data_file:s0";
    if(output < 0 || fchown(output,1017,1017) ||
       fsetxattr(output,"security.selinux",label,strlen(label)+1,0) ||
       dup2(output,1)<0 || dup2(output,2)<0) die("keystore log");
    close(output);
    if (setgroups(0, NULL) || setgid(1017) || setuid(1017)) die("keystore uid");
    if(diagnostic)diagnostic[0]=2;
    const char *context = "u:r:keystore:s0";
    int fd = open("/proc/self/attr/current", O_WRONLY | O_CLOEXEC);
    if (fd < 0 || write(fd, context, strlen(context)) != (ssize_t)strlen(context)) die("exec context");
    close(fd);
    if(diagnostic)diagnostic[0]=3;
    fputs("executing guest keystore2 as uid 1017\n", stderr);
    /* No directory makes main abort before creating any service or database. */
    if (argc == 2) execl("/system/bin/keystore2", "/system/bin/keystore2", (char *)0);
    else execl("/system/bin/keystore2", "/system/bin/keystore2", "/data/misc/keystore", (char *)0);
    die("exec keystore2");
}
