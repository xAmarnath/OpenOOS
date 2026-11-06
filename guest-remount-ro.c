#define _GNU_SOURCE
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    const char *root = "/data/local/tmp/gsi17-graphics-20261105-01/rootfs/";
    char target[PATH_MAX];
    struct statvfs st;
    if (argc != 2 || !realpath(argv[1], target)) return 2;
    if (strncmp(target, root, strlen(root))) return 3;
    if (statvfs(target, &st)) { perror("statvfs"); return 4; }
    unsigned long flags = MS_BIND | MS_REMOUNT | MS_RDONLY;
    if (st.f_flag & ST_NOSUID) flags |= MS_NOSUID;
    if (st.f_flag & ST_NODEV) flags |= MS_NODEV;
    if (st.f_flag & ST_NOEXEC) flags |= MS_NOEXEC;
    if (mount(NULL, target, NULL, flags, NULL)) { perror(target); return 5; }
    return 0;
}
