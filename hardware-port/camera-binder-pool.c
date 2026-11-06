extern void *dlsym(void *, const char *);
extern long write(int, const void *, unsigned long);
static int (*camera_main)(int,char**,char**);
static int guest_main(int argc,char **argv,char **env) {
 void (*start)(void)=dlsym((void*)0,"ABinderProcess_startThreadPool");
 if(start) {start(); const char ok[]="camera Binder pool started before main\n";write(2,ok,sizeof(ok)-1);}
 return camera_main(argc,argv,env);
}
__attribute__((noreturn)) void __libc_init(void *raw,void (*onexit)(void),int (*main)(int,char**,char**),void *structors) {
 camera_main=main;
 void (*init)(void*,void(*)(void),int(*)(int,char**,char**),void*)=dlsym((void*)-1,"__libc_init");
 init(raw,onexit,guest_main,structors);
 __builtin_trap();
}
