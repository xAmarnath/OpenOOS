#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <poll.h>
static pid_t supplicant_pid;
static int reset(const char *name,const char *value){
 pid_t p=fork();if(!p){execl("/data/local/tmp/magisk","magisk","resetprop","-n",name,value,(char*)0);_exit(127);}
 int s;return p>0&&waitpid(p,&s,0)==p&&WIFEXITED(s)&&!WEXITSTATUS(s)?0:-1;
}
static int restore_guest_data(const char *path){
 const char *allowed[]={"/data/system_ce/0","/data/vendor_ce/0","/data/misc_ce/0"};
 char resolved[PATH_MAX];int ok=0;
 for(int i=0;i<3;i++)if(!strcmp(path,allowed[i]))ok=1;
 if(!ok||!realpath(path,resolved)||strcmp(path,resolved))return -1;
 pid_t p=fork();if(!p){execl("/system/bin/restorecon","restorecon","-R",path,(char*)0);_exit(127);}
 int s;if(p<=0||waitpid(p,&s,0)!=p||!WIFEXITED(s)||WEXITSTATUS(s))return -1;
 return reset("selinux.restorecon_recursive",path);
}
static int receive(int fd,void *buf,size_t len){char *p=buf;while(len){ssize_t n=read(fd,p,len);if(n<=0)return -1;p+=n;len-=n;}return 0;}
static int string(int fd,char *s,uint32_t cap){uint32_t n;if(receive(fd,&n,4)||n>=cap)return -1;if(receive(fd,s,n))return -1;s[n]=0;return 0;}
static void reap_supplicant(void){
 if(supplicant_pid>0 && waitpid(supplicant_pid,0,WNOHANG)==supplicant_pid){
  supplicant_pid=0;reset("init.svc.wpa_supplicant","stopped");
  unlink("/dev/socket/wpa_wlan0");
 }
}
static int supplicant_control(int start){
 reap_supplicant();
 if(!start){
  if(supplicant_pid>0){
   kill(supplicant_pid,SIGTERM);
   for(int i=0;i<20 && supplicant_pid>0;i++){usleep(50000);reap_supplicant();}
   if(supplicant_pid>0){kill(supplicant_pid,SIGKILL);waitpid(supplicant_pid,0,0);supplicant_pid=0;}
  }
  unlink("/dev/socket/wpa_wlan0");
  return reset("init.svc.wpa_supplicant","stopped");
 }
 if(supplicant_pid>0)return 0;
 supplicant_pid=fork();
 if(!supplicant_pid){
  struct sockaddr_un a={.sun_family=AF_UNIX};
  strcpy(a.sun_path,"/dev/socket/wpa_wlan0");
  unlink(a.sun_path);
  int fd=socket(AF_UNIX,SOCK_DGRAM,0);
  if(fd<0 || bind(fd,(void*)&a,sizeof(a)) || chown(a.sun_path,1010,1010) || chmod(a.sun_path,0660))_exit(126);
  char value[32];snprintf(value,sizeof(value),"%d",fd);setenv("ANDROID_SOCKET_wpa_wlan0",value,1);
  execl("/vendor/bin/hw/wpa_supplicant","wpa_supplicant","-O/data/vendor/wifi/wpa/sockets","-puse_p2p_group_interface=1","-dd","-g@android:wpa_wlan0",(char*)0);
  _exit(127);
 }
 return supplicant_pid>0?reset("init.svc.wpa_supplicant","running"):-1;
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("gsi-props-20261106-wifi-aidl-v1");return 0;}
 pid_t service_pids[3]={0,0,0};
 const char *service_names[]={"aidl/apexservice","aidl/artd","idmap2d"};
 const char *service_controls[]={"ctl.interface_start","ctl.interface_start","ctl.start"};
 const char *service_bins[]={"/system/bin/apexd","/apex/com.android.art/bin/artd","/system/bin/idmap2d"};
 struct stat st;if(stat("/data/local/tmp/guest-runtime-only",&st))return 2;
 int server=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,"/dev/socket/property_service");
 if(bind(server,(void*)&a,sizeof(a))||chmod(a.sun_path,0666)||listen(server,16)){perror("property bind");return 1;}
 for(;;){reap_supplicant();struct pollfd pfd={.fd=server,.events=POLLIN};if(poll(&pfd,1,1000)<=0)continue;int c=accept4(server,0,0,SOCK_CLOEXEC);if(c<0)continue;struct timeval tv={.tv_sec=3};setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
 uint32_t cmd,status=0x000b;char name[256],value[4096];
 if(!receive(c,&cmd,4)&&cmd==0x20001&&!string(c,name,sizeof(name))&&!string(c,value,sizeof(value))){
 if(!strcmp(name,"selinux.restorecon_recursive")){
  if(!*value){if(!reset(name,value))status=0;}
  else if(!restore_guest_data(value))status=0;
 }
 if(strncmp(name,"ctl.",4)&&strcmp(name,"sys.powerctl")&&strncmp(name,"selinux.",8)){
 if(!reset(name,value))status=0;
 }
 if(!strcmp(name,"ctl.interface_start")||!strcmp(name,"ctl.start")){
 for(int i=0;i<3;i++)if(!strcmp(name,service_controls[i])&&!strcmp(value,service_names[i])){
 if(service_pids[i]>0 && waitpid(service_pids[i],0,WNOHANG)!=0)service_pids[i]=0;
 if(service_pids[i]<=0){
  service_pids[i]=fork();if(!service_pids[i]){execl(service_bins[i],service_bins[i],(char*)0);_exit(127);}
 }
 if(service_pids[i]>0)status=0;
 }
 }
 if(((!strcmp(name,"ctl.start")||!strcmp(name,"ctl.stop"))&&!strcmp(value,"wpa_supplicant")) ||
    ((!strcmp(name,"ctl.interface_start")||!strcmp(name,"ctl.interface_stop"))&&
     (!strcmp(value,"aidl/android.hardware.wifi.supplicant.ISupplicant/default")||
      !strcmp(value,"aidl/vendor.qti.hardware.wifi.supplicant.ISupplicantVendor/default")))){
  if(!supplicant_control(!strcmp(name,"ctl.start")||!strcmp(name,"ctl.interface_start")))status=0;
 }
 /* Guest ADB is disabled. The external host transport supervises this
  * container, so acknowledge its lifecycle without touching USB or host init. */
 if((!strcmp(name,"ctl.start")||!strcmp(name,"ctl.stop"))&&!strcmp(value,"adbd"))status=0;
 fprintf(stderr,"property %s value=%s result=%u\n",name,value,status);
 }
 write(c,&status,4);close(c);
 }
}
