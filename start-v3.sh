#!/system/bin/sh
set -eu
S=/data/local/tmp/gsi17-graphics-20261105-01
B=$S/busybox
if test "${GSI_START_CLEAN_FDS:-0}" != 1; then
    export GSI_START_CLEAN_FDS=1
    exec "$S/host-fd-clean" /system/bin/sh "$S/start-v3.sh" "$@"
fi
RUN=$1
case "$RUN" in "$S"/attempts/*) ;; *) exit 2;; esac
test "$(cat "$RUN/boot.id")" = "$(cat /proc/sys/kernel/random/boot_id)"
test "$(cat "$S/namespace.boot")" = "$(cat "$RUN/boot.id")"
test "$(readlink /proc/self/ns/mnt)" = "$(readlink /proc/1/ns/mnt)"
keeper=$(cat "$S/namespace.pid")
test "$(readlink /proc/$keeper/ns/mnt)" = "$(cat "$S/namespace.id")"
test ! -f "$RUN/supervisor.pid"
test -s "$RUN/baseline.policy"
test -s "$RUN/framework.policy"
M=$S/magiskpolicy
"$M" --load "$RUN/baseline.raw" --print-rules > "$RUN/baseline.unsorted"
"$B" sort "$RUN/baseline.unsorted" > "$RUN/baseline.rules"
"$M" --load /sys/fs/selinux/policy --print-rules > "$RUN/current.unsorted"
"$B" sort "$RUN/current.unsorted" > "$RUN/current.rules"
test -s "$RUN/baseline.rules"
cmp "$RUN/baseline.rules" "$RUN/current.rules"
for svc in vendor.nfc_hal_service_v2 vendor.bluetooth-aidl-qti wpa_supplicant vendor.wifi_hal_legacy netd vendor.qti.hardware.display.allocator vendor.qti.hardware.display.composer surfaceflinger zygote zygote_next zygote_third; do
    getprop "init.svc.$svc" > "$RUN/$svc.before"
done
# Snapshot network runtime knobs for restoration after a shared-network run.
: > "$RUN/network-sysctls.before"
for file in /proc/sys/net/ipv4/conf/*/* /proc/sys/net/ipv6/conf/*/*; do
    value=
    IFS= read -r value < "$file" 2>/dev/null || test -n "$value" || continue
    printf '%s %s\n' "$file" "$value" >> "$RUN/network-sysctls.before"
done
export GSI_RUN=$RUN
setsid "$S/guest-supervisor" "$RUN" 0 "$S/recover-v3.sh" /system/bin/sh "$S/worker-v3.sh" </dev/null > "$RUN/supervisor.log" 2>&1 &
echo "started supervisor=$! run=$RUN"
