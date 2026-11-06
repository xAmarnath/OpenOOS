#define _GNU_SOURCE
#include <linux/bpf.h>
#include <sys/syscall.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include "guest-fd-hygiene.h"

#define BASE "/data/local/tmp/gsi17-graphics-20261105-01/attempts/"
#define MAP "/sys/fs/bpf/netd_shared/map_netd_uid_owner_map"
#define BACKGROUND 4096U
#define PERMMAP "/sys/fs/bpf/netd_shared/map_netd_uid_permission_chunk_map"
struct value { uint32_t iif,rule; };
struct entry { uint32_t uid,existed; struct value before; };
struct header { char magic[8],boot[40]; uint32_t mapid; };
static int bpf(int cmd,union bpf_attr *a){return syscall(__NR_bpf,cmd,a,sizeof(*a));}
static int readboot(char *out){FILE *f=fopen("/proc/sys/kernel/random/boot_id","r");if(!f)return -1;char *r=fgets(out,40,f);fclose(f);return r?0:-1;}
static int lookup(int fd,uint32_t uid,struct value *v){union bpf_attr a={.map_fd=fd,.key=(uintptr_t)&uid,.value=(uintptr_t)v};return bpf(BPF_MAP_LOOKUP_ELEM,&a);}
static int update(int fd,uint32_t uid,struct value *v){union bpf_attr a={.map_fd=fd,.key=(uintptr_t)&uid,.value=(uintptr_t)v,.flags=BPF_ANY};return bpf(BPF_MAP_UPDATE_ELEM,&a);}
static int del(int fd,uint32_t uid){union bpf_attr a={.map_fd=fd,.key=(uintptr_t)&uid};return bpf(BPF_MAP_DELETE_ELEM,&a);}
// Verified against this phone's loaded netd.o: 128 words, 9 UIDs per
// word, 7 permission bits per UID. Bit 2 means INTERNET is denied.
// Never replace a whole chunk from a journal: preserve all unrelated UIDs.
static int sockets(const char *run,int restore,uint32_t *uids,size_t count){
 char path[512],boot[40]={0};if(readboot(boot))return 1;
 snprintf(path,sizeof(path),"%s/boot.id",run);FILE *bf=fopen(path,"r");char saved[40]={0};
 if(!bf)return 1;fgets(saved,sizeof(saved),bf);fclose(bf);if(strcmp(saved,boot))return 1;
 union bpf_attr a={.pathname=(uintptr_t)PERMMAP};int fd=bpf(BPF_OBJ_GET,&a);if(fd<0)return 1;
 struct bpf_map_info info={0};memset(&a,0,sizeof(a));a.info.bpf_fd=fd;a.info.info_len=sizeof(info);a.info.info=(uintptr_t)&info;
 if(bpf(BPF_OBJ_GET_INFO_BY_FD,&a)||info.type!=BPF_MAP_TYPE_HASH||info.key_size!=4||info.value_size!=1024||info.max_entries!=900){close(fd);return 1;}
 snprintf(path,sizeof(path),"%s/uid-sockets.journal",run);int j=open(path,O_RDWR|O_CLOEXEC|O_NOFOLLOW|(restore?0:O_CREAT),0600);
 if(j<0){close(fd);return restore&&errno==ENOENT?0:1;}
 if(flock(j,LOCK_EX)){close(j);close(fd);return 1;}
 struct stat st;fstat(j,&st);struct header h={.magic="GSISOCK",.mapid=info.id};memcpy(h.boot,boot,40);
 if(!st.st_size){if(restore||write(j,&h,sizeof(h))!=sizeof(h)||fsync(j))goto fail;}
 else{struct header old;if(read(j,&old,sizeof(old))!=sizeof(old)||memcmp(&h,&old,sizeof(h)))goto fail;}
 uint32_t rows[4096];size_t n=0;ssize_t got;
 while((got=read(j,&rows[n],4))>0){if(got!=4||++n>=4096)goto fail;}if(got<0)goto fail;
 if(restore){uids=rows;count=n;}
 for(size_t i=0;i<count;i++){
  uint32_t uid=uids[i];if(uid<10000||uid>99999)continue;
  uint32_t key=uid/1152,word=(uid/9)%128;uint64_t mask=4ULL<<((uid%9)*7),v[128];
  memset(&a,0,sizeof(a));a.map_fd=fd;a.key=(uintptr_t)&key;a.value=(uintptr_t)v;
  if(bpf(BPF_MAP_LOOKUP_ELEM,&a)){if(errno==ENOENT)continue;goto fail;}
  if(restore){v[word]|=mask;}
  else{
   if(!(v[word]&mask))continue;
   size_t k=0;for(;k<n;k++)if(rows[k]==uid)break;
   if(k==n){if(n>=4095||write(j,&uid,4)!=4||fsync(j))goto fail;rows[n++]=uid;}
   v[word]&=~mask;
  }
  a.flags=BPF_EXIST;if(bpf(BPF_MAP_UPDATE_ELEM,&a))goto fail;
  printf("%s guest INTERNET permission uid=%u\n",restore?"Restored":"Enabled",uid);fflush(stdout);
 }
 close(j);close(fd);return 0;
fail:perror("guest socket network");close(j);close(fd);return 1;
}
static void collect(int dir,uint32_t *uids,size_t *n,int depth){
 if(depth>12||*n>=4095)return;
 int pf=openat(dir,"cgroup.procs",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);FILE *f=pf<0?NULL:fdopen(pf,"r");int pid;
 if(f){while(fscanf(f,"%d",&pid)==1&&*n<4095){
  char path[80],line[2048];snprintf(path,sizeof(path),"/proc/%d/status",pid);FILE *p=fopen(path,"r");if(!p)continue;
  unsigned uid=0;int internet=0;
  while(fgets(line,sizeof(line),p)){if(sscanf(line,"Uid: %u",&uid)==1)continue;
   if(!strncmp(line,"Groups:",7)){char *pos=line+7,*end;while(*pos){unsigned long gid=strtoul(pos,&end,10);if(pos==end)break;if(gid==3003)internet=1;pos=end;}}}
  fclose(p);if(internet&&uid>=10000&&uid<=99999){size_t i=0;for(;i<*n;i++)if(uids[i]==uid)break;if(i==*n)uids[(*n)++]=uid;}
 }fclose(f);}
 DIR *d=fdopendir(dup(dir));if(!d)return;struct dirent *e;
 while((e=readdir(d))){if(e->d_name[0]=='.')continue;int sub=openat(dir,e->d_name,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(sub<0)continue;collect(sub,uids,n,depth+1);close(sub);}
 closedir(d);
}
static int change(const char *run,int restore,uint32_t *uids,size_t count){
    char path[512],boot[40]={0};
    if(readboot(boot))return 1;
    snprintf(path,sizeof(path),"%s/boot.id",run);FILE *bf=fopen(path,"r");char saved[40]={0};
    if(!bf)return 1;fgets(saved,sizeof(saved),bf);fclose(bf);if(strcmp(saved,boot))return 1;
    union bpf_attr a={.pathname=(uintptr_t)MAP};int fd=bpf(BPF_OBJ_GET,&a);if(fd<0){perror("map open");return 1;}
    struct bpf_map_info info={0};memset(&a,0,sizeof(a));a.info.bpf_fd=fd;a.info.info_len=sizeof(info);a.info.info=(uintptr_t)&info;
    if(bpf(BPF_OBJ_GET_INFO_BY_FD,&a)||info.type!=BPF_MAP_TYPE_HASH||info.key_size!=4||info.value_size!=8){close(fd);return 1;}
    snprintf(path,sizeof(path),"%s/uid-background.journal",run);
    int j=open(path,O_RDWR|O_CLOEXEC|O_NOFOLLOW|(restore?0:O_CREAT),0600);
    if(j<0){close(fd);return restore&&errno==ENOENT?0:1;}
    if(flock(j,LOCK_EX)){close(j);close(fd);return 1;}
    struct stat st;fstat(j,&st);struct header h={.magic="GSIUID1",.mapid=info.id};memcpy(h.boot,boot,40);
    if(!st.st_size){if(restore||write(j,&h,sizeof(h))!=sizeof(h)||fsync(j))goto fail;}
    else {struct header old;if(read(j,&old,sizeof(old))!=sizeof(old)||memcmp(&h,&old,sizeof(h)))goto fail;}
    struct entry rows[4096];size_t n=0;ssize_t got;
    while((got=read(j,&rows[n],sizeof(rows[n])))>0){if(got!=sizeof(rows[n])||++n>=4096)goto fail;}if(got<0)goto fail;
    if(restore){
        for(size_t i=0;i<n;i++){
            struct entry *e=&rows[i];struct value now={0};
            if(lookup(fd,e->uid,&now)){if(errno==ENOENT)continue;goto fail;}
            // Remove only our allowance. Preserve concurrent changes to other bits.
            now.rule=(now.rule&~BACKGROUND)|(e->before.rule&BACKGROUND);
            if(!e->existed&&!now.iif&&!now.rule){if(del(fd,e->uid)&&errno!=ENOENT)goto fail;}
            else if(update(fd,e->uid,&now))goto fail;
        }
        printf("Restored background allowances for %zu guest UIDs\n",n);
    }else{
        for(size_t i=0;i<count;i++){
            uint32_t uid=uids[i];if(uid<10000||uid>99999)continue;
            struct value v={0};int exists=lookup(fd,uid,&v)==0;if(!exists&&errno!=ENOENT)goto fail;
            if(v.rule&BACKGROUND)continue;
            size_t k=0;for(;k<n;k++)if(rows[k].uid==uid)break;
            if(k==n){struct entry e={.uid=uid,.existed=exists,.before=v};if(n>=4095||write(j,&e,sizeof(e))!=sizeof(e)||fsync(j))goto fail;rows[n++]=e;}
            v.rule|=BACKGROUND;if(update(fd,uid,&v))goto fail;
            printf("Enabled guest background network uid=%u\n",uid);fflush(stdout);
        }
    }
    close(j);close(fd);return 0;
fail:perror("guest uid network");close(j);close(fd);return 1;
}
int main(int argc,char **argv){
    if(guest_close_inherited_fds()||argc<3||strncmp(argv[2],BASE,strlen(BASE))||strchr(argv[2]+strlen(BASE),'/')||strstr(argv[2],".."))return 2;
    if(!strcmp(argv[1],"restore")){int a=change(argv[2],1,NULL,0);return sockets(argv[2],1,NULL,0)||a;}
    if(!strcmp(argv[1],"apply")){uint32_t uids[256];if(argc>259)return 2;for(int i=3;i<argc;i++){char *end;unsigned long v=strtoul(argv[i],&end,10);if(*end||v<10000||v>99999)return 2;uids[i-3]=v;}return change(argv[2],0,uids,argc-3)||sockets(argv[2],0,uids,argc-3);}
    if(!strcmp(argv[1],"watch")){
        char group[512];snprintf(group,sizeof(group),"/sys/fs/cgroup/gsi17-%s",argv[2]+strlen(BASE));
        for(;;){int dir=open(group,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)return 1;uint32_t uids[4096];size_t n=0;
            collect(dir,uids,&n,0);close(dir);if(change(argv[2],0,uids,n)||sockets(argv[2],0,uids,n))return 1;sleep(2);
        }
    }
    return 2;
}
