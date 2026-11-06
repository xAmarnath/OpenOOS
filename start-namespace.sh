#!/system/bin/sh
set -eu
S=/data/local/tmp/gsi17-graphics-20261105-01
B=$S/busybox
test "$(readlink -f "$S")" = "$S"
test "$(readlink /proc/self/ns/mnt)" = "$(readlink /proc/1/ns/mnt)"
if test -f "$S/namespace.pid"; then
    pid=$(cat "$S/namespace.pid")
    case "$pid" in ''|*[!0-9]*) exit 2;; esac
    if test "$(cat "$S/namespace.boot" 2>/dev/null)" = "$(cat /proc/sys/kernel/random/boot_id)" &&
       test "$(readlink /proc/$pid/ns/mnt 2>/dev/null)" = "$(cat "$S/namespace.id" 2>/dev/null)"; then
        echo "Existing namespace keeper=$pid"
        exit 0
    fi
fi
rm -f "$S/close-namespace" "$S/close-extended-namespace"
echo preparing > "$S/namespace.state"
setsid "$S/host-fd-clean" "$B" unshare -m /system/bin/sh "$S/namespace-v3.sh" </dev/null > "$S/namespace-v3.log" 2>&1 &
echo "Preparing namespace pid=$!"
