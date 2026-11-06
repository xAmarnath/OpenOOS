#define _GNU_SOURCE
#include <gbinder.h>
#include <gutil_log.h>
#include <glib-unix.h>
#include <sys/stat.h>
#include <string.h>
#include "guest-fd-hygiene.h"

static gboolean stop_loop(gpointer loop) {
    g_main_loop_quit(loop);
    return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv) {
    const char *guest = "/data/local/tmp/gsi17-graphics-20261105-01/rootfs/dev/binderfs/binder";
    struct stat a,b;
    if (argc != 2 || strcmp(argv[1],guest) || guest_close_inherited_fds()) return 2;
    if (stat(guest,&a) || stat("/dev/binder",&b) || a.st_rdev == b.st_rdev) return 3;
    gutil_log_default.level=GLOG_LEVEL_VERBOSE;
    GBinderServiceManager *src=gbinder_servicemanager_new(guest);
    GBinderServiceManager *dst=gbinder_servicemanager_new("/dev/binder");
    if (!src || !dst) return 4;
    const char *services[] = {"android.hardware.bluetooth.IBluetoothHci/default"};
    GBinderBridge *bridges[G_N_ELEMENTS(services)];
    for (unsigned i=0;i<G_N_ELEMENTS(services);i++) {
        bridges[i]=gbinder_bridge_new3(services[i],services[i],src,dst);
        if (!bridges[i]) return 5;
    }
    GMainLoop *loop=g_main_loop_new(NULL,TRUE);
    g_unix_signal_add(SIGTERM,stop_loop,loop);
    g_unix_signal_add(SIGINT,stop_loop,loop);
    g_main_loop_run(loop);
    for (unsigned i=0;i<G_N_ELEMENTS(services);i++) gbinder_bridge_free(bridges[i]);
    g_main_loop_unref(loop);
    gbinder_servicemanager_unref(src);
    gbinder_servicemanager_unref(dst);
    return 0;
}
