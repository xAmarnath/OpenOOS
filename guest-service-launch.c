#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void die(const char *s) { perror(s); exit(1); }
static void sock(const char *name, int type, mode_t mode, int cred) {
    struct sockaddr_un a={.sun_family=AF_UNIX};
    snprintf(a.sun_path,sizeof(a.sun_path),"/dev/socket/%s",name);
    int fd=socket(AF_UNIX,type,0),one=1;
    if(fd<0 || (cred && setsockopt(fd,SOL_SOCKET,SO_PASSCRED,&one,sizeof(one))) || bind(fd,(void*)&a,sizeof(a)))die("socket");
    if(chmod(a.sun_path,mode)||chown(a.sun_path,1000,1000))die("permissions");
    char label[128];
    snprintf(label,sizeof(label),"u:object_r:%s_socket:s0",name);
    if(setxattr(a.sun_path,"security.selinux",label,strlen(label)+1,0))die("socket label");
    char key[128],value[24];
    snprintf(key,sizeof(key),"ANDROID_SOCKET_%s",name);
    snprintf(value,sizeof(value),"%d",fd);
    if(setenv(key,value,1))die("environment");
}
int main(int argc,char **argv) {
    struct stat marker,self,host;
    if(stat("/data/local/tmp/guest-runtime-only",&marker))die("guest marker");
    if(stat("/proc/self/ns/net",&self)||stat("/proc/1/ns/net",&host))die("network namespace");
    if(argc!=2)return 2;
    if(self.st_ino==host.st_ino && stat("/data/local/tmp/guest-shared-network",&marker))return 2;
    if(!strcmp(argv[1],"tombstoned")) {
        sock("tombstoned_crash",SOCK_SEQPACKET,0666,0);
        sock("tombstoned_intercept",SOCK_SEQPACKET,0666,0);
        sock("tombstoned_java_trace",SOCK_SEQPACKET,0666,0);
        execl("/system/bin/tombstoned","/system/bin/tombstoned",(char*)0);
    } else if(!strcmp(argv[1],"statsd")) {
        sock("statsdw",SOCK_DGRAM,0222,1);
        execl("/apex/com.android.os.statsd/bin/statsd","/apex/com.android.os.statsd/bin/statsd",(char*)0);
    } else if(!strcmp(argv[1],"lmkd")) {
        sock("lmkd",SOCK_SEQPACKET,0660,1);
        execl("/system/bin/lmkd","/system/bin/lmkd",(char*)0);
    } else return 2;
    die("exec");
}
