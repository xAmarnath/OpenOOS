#ifndef GUEST_FD_HYGIENE_H
#define GUEST_FD_HYGIENE_H
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Guest services create their own Android sockets after exec. Only the
 * launcher's standard streams are intentional inherited descriptors.
 * sux currently passes >4100 descriptors; QTI input_fence accepts <=4096.
 * Close our copies before the service allocates buffers and sync fences.
 */
static int guest_close_inherited_fds(void) {
#ifdef SYS_close_range
    if (syscall(SYS_close_range, 3U, ~0U, 0U) == 0) return 0;
    if (errno != ENOSYS && errno != EINVAL) return -1;
#endif
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) return -1;
    int scan_fd = dirfd(dir);
    struct dirent *entry;
    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (!entry) {
            int saved = errno;
            closedir(dir);
            errno = saved;
            return saved ? -1 : 0;
        }
        char *end;
        long fd = strtol(entry->d_name, &end, 10);
        if (*end || fd < 3 || fd > INT_MAX || fd == scan_fd) continue;
        if (close((int)fd) && errno != EBADF && errno != EINTR) {
            int saved = errno;
            closedir(dir);
            errno = saved;
            return -1;
        }
    }
}
#endif
