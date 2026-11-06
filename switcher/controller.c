#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>

#define S "/data/local/tmp/gsi17-graphics-20261105-01"
#define D S "/switcher"
#define APP "org.openoos.switcher"
#define SOCK "openoos.switch.v1"

static pid_t worker;
static int worker_failed;
static struct stat host_root;
static unsigned host_uid, guest_uid;

static int read_text(const char *p, char *s, size_t n) {
    int f=open(p,O_RDONLY|O_CLOEXEC|O_NOFOLLOW); if(f<0)return -1;
    ssize_t r=read(f,s,n-1); close(f); if(r<0)return -1;
    s[r]=0; while(r>0 && (s[r-1]=='\n'||s[r-1]=='\r'))s[--r]=0;
    return (int)r;
}
static int write_file(const char *p, const void *s, size_t n) {
    int f=open(p,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600); if(f<0)return -1;
    size_t done=0; while(done<n) { ssize_t r=write(f,(const char*)s+done,n-done); if(r<0&&errno==EINTR)continue; if(r<=0){close(f);return -1;}done+=r; }
    int rc=fsync(f); if(rc && errno==EINVAL)rc=0; if(close(f))rc=-1; return rc;
}
static int run_state(char *run,size_t n,char *state,size_t sn) {
    if(read_text(S "/active-run",run,n)<0){strcpy(state,"none");return 0;}
    const char *prefix=S "/attempts/";
    if(strncmp(run,prefix,strlen(prefix)) || !run[strlen(prefix)])return -1;
    for(const char *p=run+strlen(prefix);*p;p++)if(!isalnum((unsigned char)*p)&&*p!='-')return -1;
    char path[640],saved[80],boot[80];
    snprintf(path,sizeof(path),"%s/boot.id",run);
    if(read_text(path,saved,sizeof(saved))<0 || read_text("/proc/sys/kernel/random/boot_id",boot,sizeof(boot))<0)return -1;
    if(strcmp(saved,boot)){strcpy(state,"previous-boot");return 0;}
    snprintf(path,sizeof(path),"%s/state",run);
    if(read_text(path,state,sn)<0)strcpy(state,"preparing");
    return 0;
}
/* Match the kernel-supplied UID and the process's actual filesystem root.
 * Host and guest Android can allocate the same app UID independently. */
static const char *authenticate(int fd) {
    struct ucred c; socklen_t len=sizeof(c);
    if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&c,&len))return NULL;
    char path[700],name[256]; struct stat peer,expected;
    snprintf(path,sizeof(path),"/proc/%d/root",c.pid);
    if(stat(path,&peer))return NULL;
    int host=peer.st_dev==host_root.st_dev && peer.st_ino==host_root.st_ino;
    if(c.uid==0 && host)return "STOCK"; /* root-only diagnostic client */
    if(c.uid!=(host?host_uid:guest_uid))return NULL;
    snprintf(path,sizeof(path),"/proc/%d/cmdline",c.pid);
    if(read_text(path,name,sizeof(name))<0 || strcmp(name,APP))return NULL;
    if(host)return "STOCK";
    char run[512],state[64],pid[32];
    if(run_state(run,sizeof(run),state,sizeof(state)) || strcmp(state,"guest-running"))return NULL;
    snprintf(path,sizeof(path),"%s/system-preflight.pid",run);
    if(read_text(path,pid,sizeof(pid))<1)return NULL;
    for(char *p=pid;*p;p++)if(!isdigit((unsigned char)*p))return NULL;
    snprintf(path,sizeof(path),"/proc/%s/root" S "/rootfs",pid);
    if(stat(path,&expected) || peer.st_dev!=expected.st_dev || peer.st_ino!=expected.st_ino)return NULL;
    return "GUEST";
}
static int patch_policy(const char *in,const char *out) {
    unsigned char header[24];
    int f=open("/odm/etc/selinux/precompiled_sepolicy",O_RDONLY|O_CLOEXEC); if(f<0)return 1;
    ssize_t r=read(f,header,24); close(f);
    if(r!=24 || memcmp(header+8,"SE Linux",8))return 1;
    uint32_t flags; memcpy(&flags,header+20,4); flags &= 0xc0000000U;
    f=open(in,O_RDONLY|O_CLOEXEC|O_NOFOLLOW); if(f<0)return 1;
    struct stat st; if(fstat(f,&st)||!S_ISREG(st.st_mode)||st.st_size<=24||st.st_size>=4194304){close(f);return 1;}
    size_t size=(size_t)st.st_size; unsigned char *data=malloc(size); if(!data){close(f);return 1;}
    size_t done=0;while(done<size){r=read(f,data+done,size-done);if(r<0&&errno==EINTR)continue;if(r<=0)break;done+=r;}close(f);
    if(done!=size||memcmp(data+8,"SE Linux",8)){free(data);return 1;}
    uint32_t word;memcpy(&word,data+20,4);word|=flags;memcpy(data+20,&word,4);
    int rc=write_file(out,data,size);free(data);return rc?1:0;
}
static int connect_socket(void) {
    int f=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(f<0)return -1;
    struct sockaddr_un a={.sun_family=AF_UNIX};memcpy(a.sun_path+1,SOCK,sizeof(SOCK)-1);
    if(connect(f,(void*)&a,offsetof(struct sockaddr_un,sun_path)+sizeof(SOCK))){close(f);return -1;}return f;
}
int main(int argc,char **argv) {
    if(argc==3&&!strcmp(argv[1],"--request")) {
        int f=connect_socket();if(f<0){perror("connect");return 1;}
        dprintf(f,"%s\n",argv[2]);char buf[2048];ssize_t n=read(f,buf,sizeof(buf));if(n>0)write(1,buf,n);close(f);return n>0?0:1;
    }
    if(getuid()!=0)return 2;
    if(argc==4&&!strcmp(argv[1],"--patch"))return patch_policy(argv[2],argv[3]);
    if(argc!=2||strcmp(argv[1],"--daemon"))return 2;
    struct stat init_root,ns1,ns2;
    if(stat("/",&host_root)||stat("/proc/1/root",&init_root)||host_root.st_dev!=init_root.st_dev||host_root.st_ino!=init_root.st_ino || stat("/proc/self/ns/mnt",&ns1)||stat("/proc/1/ns/mnt",&ns2)||ns1.st_ino!=ns2.st_ino)return 3;
    int lock=open(D "/controller.lock",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(lock<0||flock(lock,LOCK_EX|LOCK_NB))return 4;
    char config[128];if(read_text(D "/uids",config,sizeof(config))<0||sscanf(config,"%u %u",&host_uid,&guest_uid)!=2||host_uid<10000||guest_uid<10000)return 5;
    /* Keep the controller alive when either Android's app cgroups are drained. */
    char pid[32];snprintf(pid,sizeof(pid),"%d\n",getpid());
    int cg=open("/sys/fs/cgroup/cgroup.procs",O_WRONLY|O_CLOEXEC);
    if(cg<0||write(cg,pid,strlen(pid))!=(ssize_t)strlen(pid))return 6;close(cg);
    if(write_file(D "/controller.pid",pid,strlen(pid)))return 7;
    signal(SIGPIPE,SIG_IGN);umask(077);
    int server=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(server<0)return 8;
    struct sockaddr_un a={.sun_family=AF_UNIX};memcpy(a.sun_path+1,SOCK,sizeof(SOCK)-1);
    if(bind(server,(void*)&a,offsetof(struct sockaddr_un,sun_path)+sizeof(SOCK))||listen(server,16)){perror("bind/listen");return 9;}
    for(;;) {
        if(worker){int st;pid_t p=waitpid(worker,&st,WNOHANG);if(p==worker){worker_failed=!WIFEXITED(st)||WEXITSTATUS(st)!=0;worker=0;}}
        struct pollfd p={.fd=server,.events=POLLIN};if(poll(&p,1,500)<=0)continue;
        int fd=accept4(server,NULL,NULL,SOCK_CLOEXEC);if(fd<0)continue;
        struct timeval tv={.tv_sec=2};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
        const char *env=authenticate(fd);
        if(!env){dprintf(fd,"DENIED\n");close(fd);continue;}
        char cmd[32];size_t used=0;while(used<sizeof(cmd)-1){char c;ssize_t n=read(fd,&c,1);if(n!=1)break;if(c=='\n')break;cmd[used++]=c;}cmd[used]=0;
        char run[512]={0},state[64]={0};int bad=run_state(run,sizeof(run),state,sizeof(state));
        int guest=!strcmp(state,"guest-running"),stock=!strcmp(state,"recovered")||!strcmp(state,"none")||!strcmp(state,"previous-boot");
        if(!strcmp(cmd,"GUEST")||!strcmp(cmd,"STOCK")) {
            int allowed=(!strcmp(cmd,"GUEST")&&!strcmp(env,"STOCK")&&stock)||(!strcmp(cmd,"STOCK")&&!strcmp(env,"GUEST")&&guest);
            if(!worker&&!bad&&allowed) {
                worker_failed=0;worker=fork();
                if(worker==0){close(fd);close(server);close(lock);execl("/system/bin/sh","sh",D "/transition.sh",cmd,(char*)NULL);_exit(127);}
                if(worker<0){worker=0;worker_failed=1;}
            }
        }else if(strcmp(cmd,"STATUS")){dprintf(fd,"ERROR\tUnknown command\n");close(fd);continue;}
        const char *status="READY",*msg=guest?"Guest session is running":"Stock Android is running";
        if(worker){status="BUSY";msg="Switching Android environments…";}
        else if(bad||(!guest&&!stock)){status="ERROR";msg="Transition needs attention. Check the runtime logs.";}
        else if(worker_failed){msg="Last switch did not finish. You can retry.";}
        dprintf(fd,"OK\t%s\t%s\t%s\n",env,status,msg);close(fd);
    }
}
