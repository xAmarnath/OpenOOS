#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>

static void die(const char *s) { perror(s); exit(1); }
static void make_socket(const char *name) {
    struct sockaddr_un addr={.sun_family=AF_UNIX};
    snprintf(addr.sun_path,sizeof(addr.sun_path),"/dev/socket/%s",name);
    int fd=socket(AF_UNIX,SOCK_STREAM,0);
    if(fd<0) die("socket");
    /* Refuse to replace any existing socket, including stock sockets. */
    if(bind(fd,(struct sockaddr*)&addr,sizeof(addr))) die("bind");
    if(chmod(addr.sun_path,0660)||chown(addr.sun_path,0,1000)) die("socket permissions");
    char key[128],value[24];
    snprintf(key,sizeof(key),"ANDROID_SOCKET_%s",name);
    snprintf(value,sizeof(value),"%d",fd);
    setenv(key,value,1);
}
int main(int argc, char **argv) {
    (void)argv;
    struct stat marker;
    if(stat("/data/local/tmp/guest-runtime-only",&marker)) die("guest marker");
    DIR *fds=opendir("/proc/self/fd");
    if(!fds) die("fds");
    struct dirent *ent;
    while((ent=readdir(fds))) { int fd=atoi(ent->d_name); if(fd>2 && fd!=dirfd(fds))close(fd); }
    closedir(fds);
    int input=open("/dev/null",O_RDWR); if(input<0)die("stdin"); if(dup2(input,0)<0)die("dup stdin");if(dup2(input,1)<0||dup2(input,2)<0)die("dup output");if(input>2)close(input);
    clearenv();
    setenv("PATH","/system/bin:/apex/com.android.art/bin",1);
    setenv("ANDROID_ROOT","/system",1);
    setenv("ANDROID_DATA","/data",1);
    setenv("ANDROID_STORAGE","/storage",1);
    setenv("EXTERNAL_STORAGE","/storage/emulated/0",1);
    setenv("ANDROID_ART_ROOT","/apex/com.android.art",1);
    setenv("ANDROID_RUNTIME_ROOT","/apex/com.android.runtime",1);
    setenv("ANDROID_I18N_ROOT","/apex/com.android.i18n",1);
    setenv("ANDROID_TZDATA_ROOT","/apex/com.android.tzdata",1);
    setenv("ART_APEX_DATA","/data/misc/apexdata/com.android.art",1);
    FILE *f=fopen("/data/system/environ/classpath","r");
    if(!f) die("classpath");
    char *line=NULL;size_t n=0;
    while(getline(&line,&n,f)>0) {
        char *save,*head=strtok_r(line," \n",&save),*key=strtok_r(NULL," \n",&save),*value=strtok_r(NULL," \n",&save);
        if(!head||strcmp(head,"export")||!key||!value) die("classpath format");
        setenv(key,value,1);
    }
    free(line);fclose(f);
    make_socket("zygote");make_socket("usap_pool_primary");
    fprintf(stderr,"Launching guest ART/zygote, system_server=%d, pid=%d\n",argc>1,getpid());
    char *args[]={"/system/bin/app_process64","-Xzygote","/system/bin","--zygote",argc>1 ? "--start-system-server" : "--nice-name=zygote64","--socket-name=zygote","--abi-list=arm64-v8a",NULL};
    execv(args[0],args);die("exec");
}
