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
static void make_socket(const char *name, gid_t gid) {
    struct sockaddr_un a={.sun_family=AF_UNIX};
    snprintf(a.sun_path,sizeof(a.sun_path),"/dev/socket/%s",name);
    int fd=socket(AF_UNIX,SOCK_STREAM,0);
    if(fd<0 || bind(fd,(void*)&a,sizeof(a)))die("socket");
    if(chmod(a.sun_path,0660)||chown(a.sun_path,0,gid))die("permissions");
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
    if(self.st_ino==host.st_ino && !(argc==2 && !strcmp(argv[1],"--shared-network") && !stat("/data/local/tmp/guest-shared-network",&marker))){fputs("refusing unauthorized host network namespace\n",stderr);return 2;}
    make_socket("dnsproxyd",3003);make_socket("mdns",1000);make_socket("fwmarkd",3003);
    execl("/system/bin/netd","/system/bin/netd",(char*)0);die("exec");
}
