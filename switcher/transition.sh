#!/system/bin/sh
set -eu
S=/data/local/tmp/gsi17-graphics-20261105-01
D=$S/switcher
B=$S/busybox
umask 077
if test "${1:-}" != PREP; then exec > "$D/transition.log" 2>&1; fi
test "$(id -u)" = 0
test "$(readlink /proc/self/ns/mnt)" = "$(readlink /proc/1/ns/mnt)"
test "$(getprop ro.build.fingerprint)" = "$(cat "$D/stock.fingerprint")"
APP=org.openoos.switcher/.MainActivity

open_guest() {
    run=$1
    i=0
    while test "$i" -lt 150; do
        state=$(cat "$run/state" 2>/dev/null || true)
        case "$state" in recovering-stock|recovered|recovery-failed|stopping-guest) return 1;; esac
        # UI initialization opens Settings. Wait for it to finish so it cannot
        # replace the switcher after the switch has been reported ready.
        if test -s "$run/system-preflight.pid" && grep -qx 'OpenOOS UI configuration complete' "$run/ui-config.log" 2>/dev/null; then
            p=$(cat "$run/system-preflight.pid")
            group=/sys/fs/cgroup/gsi17-${run##*/}
            # Binder commands must inherit the guest cgroup before entering it.
            # Run only this child there; the switch controller stays on the host.
            if /system/bin/sh -c 'echo $$ > "$1/cgroup.procs" || exit 1
                exec "$2/busybox" timeout -s KILL 12 "$2/busybox" nsenter -t "$3" -m -n -- "$2/guest-root" "$2/rootfs" /system/bin/am start -W -n "$4"' sh "$group" "$S" "$p" "$APP" > "$D/open-guest.log" 2>&1; then
                if grep -q 'Status: ok' "$D/open-guest.log"; then return 0; fi
            fi
        fi
        sleep 2; i=$((i+2))
    done
    return 1
}

case "${1:-}" in
GUEST)
    "$B" flock -n "$S/attempts/supervisor.lock" /system/bin/sh "$D/transition.sh" PREP
    run=$(cat "$S/active-run")
    sh "$S/start-v3.sh" "$run"
    open_guest "$run"
    ;;
PREP)
    # Preparation is exclusive with an already-running guest supervisor.
    boot=$(cat /proc/sys/kernel/random/boot_id)
    if test -f "$S/active-run"; then
        old=$(cat "$S/active-run")
        case "$old" in "$S"/attempts/*) ;; *) exit 2;; esac
        if test "$(cat "$old/boot.id")" = "$boot"; then test "$(cat "$old/state")" = recovered; fi
    fi
    for f in /sys/fs/cgroup/gsi17-*/cgroup.events; do
        test ! -f "$f" || ! grep -q '^populated 1$' "$f" || exit 3
    done
    test "$(getprop init.svc.zygote)" = running
    test -s "$S/userdata-ext4.img"
    sh "$S/start-namespace.sh"
    i=0
    while test "$(cat "$S/namespace.state")" != ready; do
        i=$((i+1)); test "$i" -lt 45; sleep 1
    done
    test "$(cat "$S/namespace.boot")" = "$boot"
    p=$(cat "$S/namespace.pid")
    test "$(readlink /proc/$p/ns/mnt)" = "$(cat "$S/namespace.id")"
    "$B" nsenter -t "$p" -m -- test -f "$S/rootfs/system/build.prop"
    run=$S/attempts/$(printf '%s' "$boot" | cut -c 1-8)-$(date +%s)-switch-$$
    mkdir "$run"
    printf '%s\n' "$boot" > "$run/boot.id"
    cat /sys/fs/selinux/policy > "$run/baseline.raw"
    sh "$S/compile-framework-policy.sh" "$run"
    "$D/controller" --patch "$run/baseline.raw" "$run/baseline.policy"
    "$D/controller" --patch "$run/framework.raw" "$run/framework.policy"
    test ! -L "$S/active-run"; test ! -L "$S/active-run.tmp"
    printf '%s\n' "$run" > "$S/active-run.tmp"
    mv "$S/active-run.tmp" "$S/active-run"
    ;;
STOCK)
    run=$(cat "$S/active-run")
    case "$run" in "$S"/attempts/*) ;; *) exit 2;; esac
    test "$(cat "$run/boot.id")" = "$(cat /proc/sys/kernel/random/boot_id)"
    test "$(cat "$run/state")" = guest-running
    touch "$run/recover.request"
    i=0
    while test "$(cat "$run/state")" != recovered; do
        test "$(cat "$run/state")" != recovery-failed
        i=$((i+1)); test "$i" -lt 180; sleep 1
    done
    # Stock services may report ready just before the first Activity launch.
    i=0
    while test "$i" -lt 30; do
        if am start -W -n "$APP" > "$D/open-stock.log" 2>&1 && grep -q 'Status: ok' "$D/open-stock.log"; then exit 0; fi
        i=$((i+1)); sleep 1
    done
    exit 1
    ;;
*) exit 2;;
esac
