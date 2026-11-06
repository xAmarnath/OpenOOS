#define _GNU_SOURCE
#include <sched.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One recovery owner. Put the controller in a dedicated cgroup before it
 * forks. Guest /sys/fs/cgroup is bound to this subtree; v1 views are read-only.
 * cgroup.kill handles concurrent forks and kills all nested cgroups. */
static volatile sig_atomic_t interrupted;
static const char *run;
static char **worker;
static int lockfd;
static char cg[256];
static int control(const char *name, const char *value) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", cg, name);
    int fd = open(path, O_WRONLY|O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = write(fd, value, strlen(value));
    close(fd);
    return n == (ssize_t)strlen(value) ? 0 : -1;
}
static int empty_group(void) {
    char path[512], line[128];
    snprintf(path, sizeof(path), "%s/cgroup.events", cg);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int empty = 0;
    while (fgets(line, sizeof(line), f)) if (!strcmp(line, "populated 0\n")) empty = 1;
    fclose(f);
    return empty;
}
static void stop(int sig) { interrupted = sig; }
static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static void record(const char *name, const char *value) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", run, name);
    int fd = open(path, O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC, 0600);
    if (fd < 0) { perror(path); return; }
    dprintf(fd, "%s\n", value);
    close(fd);
}
static void defaults(void) {
    signal(SIGTERM, SIG_DFL); signal(SIGINT, SIG_DFL); signal(SIGHUP, SIG_DFL);
}
static int init_child(void *unused) {
    (void)unused;
    close(lockfd);
    defaults();
    /* PDEATHSIG is set by the parent-child relationship, before any worker. */
    if (prctl(PR_SET_PDEATHSIG, SIGKILL)) return 124;
    char self[32];
    snprintf(self, sizeof(self), "%ld", (long)getpid());
    if (control("cgroup.procs", self)) { perror("join cgroup"); return 125; }
    pid_t child = fork();
    if (child < 0) return 126;
    if (!child) { execv(worker[0], worker); perror("worker exec"); _exit(127); }
    int status;
    for (;;) {
        pid_t p = waitpid(-1, &status, 0);
        if (p == child) return WIFEXITED(status) ? WEXITSTATUS(status) : 128+WTERMSIG(status);
        if (p < 0 && errno != EINTR) return 126;
    }
}
int main(int argc, char **argv) {
    /* RUN SECONDS RECOVERY_SCRIPT SHELL WORKER_SCRIPT; 0 disables the timer. */
    if (argc != 6) return 2;
    run = argv[1];
    char *end;
    errno = 0;
    long seconds = strtol(argv[2], &end, 10);
    if (errno || end == argv[2] || *end || seconds < 0 || seconds > 1200) return 2;
    char path[1024], boot[80], expected[80];
    FILE *f = fopen("/proc/sys/kernel/random/boot_id", "r");
    if (!f || !fgets(boot, sizeof(boot), f)) return 3;
    fclose(f);
    snprintf(path, sizeof(path), "%s/boot.id", run);
    f = fopen(path, "r");
    if (!f || !fgets(expected, sizeof(expected), f)) return 3;
    fclose(f);
    if (strcmp(boot, expected)) { fputs("stale boot\n", stderr); return 3; }
    snprintf(path, sizeof(path), "%s/../supervisor.lock", run);
    lockfd = open(path, O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC, 0600);
    if (lockfd < 0 || flock(lockfd, LOCK_EX|LOCK_NB)) return 4;
    const char *name = strrchr(run, '/');
    if (!name || strlen(++name) > 80 || strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789-") != strlen(name)) return 4;
    snprintf(cg, sizeof(cg), "/sys/fs/cgroup/gsi17-%s", name);
    if (mkdir(cg, 0755)) { perror("create cgroup"); return 4; }
    struct statfs fs;
    if (statfs(cg, &fs) || fs.f_type != 0x63677270) return 4;
    /* Check kernel support on the empty, newly-created guest group. */
    if (control("cgroup.kill", "1")) { perror("cgroup.kill unavailable"); return 4; }
    snprintf(path, sizeof(path), "%ld", (long)getpid());
    record("supervisor.pid", path);
    struct sigaction sa = {.sa_handler=stop};
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL); sigaction(SIGINT, &sa, NULL); sigaction(SIGHUP, &sa, NULL);
    worker = argv + 4;
    char *stack = malloc(1024*1024);
    if (!stack) return 5;
    pid_t pid = clone(init_child, stack+1024*1024, SIGCHLD, NULL);
    if (pid < 0) { perror("controller clone"); return 6; }
    snprintf(path, sizeof(path), "%ld", (long)pid);
    record("controller.pid", path);
    record("session-limit-seconds", argv[2]);
    record("state", "guest-running");
    double deadline = now()+seconds;
    int status = 0, reaped = 0;
    snprintf(path, sizeof(path), "%s/recover.request", run);
    while (!interrupted && (seconds == 0 || now() < deadline) && access(path, F_OK)) {
        pid_t p = waitpid(pid, &status, WNOHANG);
        if (p == pid) { reaped = 1; break; }
        if (p < 0 && errno != EINTR) break;
        struct timespec tick = {.tv_nsec=100000000};
        nanosleep(&tick, NULL);
    }
    record("state", "stopping-guest");
    /* Stop the spawning controller even if cancellation raced its initial
     * cgroup join. WUNTRACED establishes the stop before the group kill. */
    if (!reaped) {
        if (kill(pid, SIGSTOP) && errno != ESRCH) return 7;
        pid_t p;
        do { p = waitpid(pid, &status, WUNTRACED); } while (p < 0 && errno == EINTR);
        if (p != pid) return 8;
        if (WIFEXITED(status) || WIFSIGNALED(status)) reaped = 1;
    }
    if (control("cgroup.kill", "1")) { perror("kill cgroup"); return 7; }
    if (!reaped) {
        /* PID remains our unreaped direct child: it cannot be reused. */
        if (kill(pid, SIGKILL) && errno != ESRCH) { perror("kill controller"); return 7; }
        pid_t p;
        do { p = waitpid(pid, &status, 0); } while (p < 0 && errno == EINTR);
        if (p != pid) { perror("wait controller"); return 8; }
    }
    while (!empty_group()) {
        struct timespec tick = {.tv_nsec=100000000};
        nanosleep(&tick, NULL);
    }
    snprintf(path, sizeof(path), "%d", status);
    record("guest.wait-status", path);
    record("guest-drained", "yes");
    record("state", "recovering-stock");
    pid_t recovery = fork();
    if (recovery < 0) return 9;
    if (!recovery) {
        close(lockfd); defaults();
        execl(argv[4], argv[4], argv[3], run, (char *)NULL);
        perror("recovery exec"); _exit(127);
    }
    pid_t p;
    do { p = waitpid(recovery, &status, 0); } while (p < 0 && errno == EINTR);
    int ok = p == recovery && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    record("state", ok ? "recovered" : "recovery-failed");
    close(lockfd);
    free(stack);
    return ok ? 0 : 10;
}
