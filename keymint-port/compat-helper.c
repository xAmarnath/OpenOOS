/* Runs only through guest-root in the isolated guest. Keep legacy software
 * keys available when keystore reconnects to the guarded hardware service. */
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern char *dlerror(void);
extern int access(const char *, int);
extern int dprintf(int, const char *, ...);
extern void _exit(int);
__attribute__((constructor)) static void start_compat(void) {
    if (access("/data/local/tmp/guest-runtime-only", 0)) _exit(120);
    void *lib = dlopen("libkm_compat_service.so", 2);
    int (*add)(const char *) = lib ? dlsym(lib, "addNamedKeyMintDeviceService") : 0;
    void *binder = dlopen("libbinder_ndk.so", 2);
    void (*threads)(unsigned int) = binder ? dlsym(binder, "ABinderProcess_setThreadPoolMaxThreadCount") : 0;
    void (*join)(void) = binder ? dlsym(binder, "ABinderProcess_joinThreadPool") : 0;
    if (!add || !threads || !join) { dprintf(2,"compat load: %s\n",dlerror()); _exit(121); }
    threads(4);
    int status = add("android.security.guestcompat");
    dprintf(2,"guest legacy compat registration=%d\n",status);
    if (status) _exit(122);
    join();
    _exit(123);
}
