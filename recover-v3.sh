#!/system/bin/sh
set -u
S=/data/local/tmp/gsi17-graphics-20261105-01
B=$S/busybox
RUN=$1
case "$RUN" in "$S"/attempts/*) ;; *) exit 2;; esac
test "$(cat "$RUN/boot.id")" = "$(cat /proc/sys/kernel/random/boot_id)" || exit 3
test "$(cat "$RUN/guest-drained")" = yes || exit 4
test -f "$RUN/policy-changes" || test -f "$RUN/host-changes" || exit 0
failed=0
# The supervisor killed the entire cgroup and verified populated=0.
if test -f "$RUN/uid-background.journal"; then
    "$S/guest-uid-network" restore "$RUN" || failed=1
fi
if test -f "$RUN/brightness.before"; then
    read -r level < "$RUN/brightness.before"
    case "$level" in ''|*[!0-9]*) failed=1;; *)
        if test "$level" -le 7707; then
            printf '%s\n' "$level" > /sys/class/backlight/panel0-backlight/brightness || failed=1
        else failed=1; fi;;
    esac
fi
if "$B" dd if="$RUN/baseline.policy" of=/sys/fs/selinux/load bs=4194304; then
    echo policy-restored
else
    echo policy-restore-failed
    failed=1
fi
# If preparation stopped early, stock services were never interrupted.
if test ! -f "$RUN/host-changes"; then
    exit "$failed"
fi
if test -f "$RUN/network-changes"; then
    # Guest and every bridge are already drained; release callbacks from its HAL.
    stop wpa_supplicant
    stop vendor.wifi_hal_legacy
    if test -s "$RUN/network-sysctls.before"; then
        while read -r path value; do
            case "$path" in /proc/sys/net/ipv4/conf/*/*|/proc/sys/net/ipv6/conf/*/*) ;; *) continue;; esac
            test -f "$path" || continue
            current=
            IFS= read -r current < "$path" || continue
            test "$current" = "$value" || printf '%s\n' "$value" > "$path" || true
        done < "$RUN/network-sysctls.before"
    fi
    test "$(cat "$RUN/vendor.wifi_hal_legacy.before")" != running || start vendor.wifi_hal_legacy
fi
if test -f "$RUN/nfc-changes"; then
    stop vendor.nfc_hal_service_v2
    sleep 1
    test "$(cat "$RUN/vendor.nfc_hal_service_v2.before")" != running || start vendor.nfc_hal_service_v2
fi
if test -f "$RUN/bluetooth-changes"; then
    stop vendor.bluetooth-aidl-qti
    sleep 1
    test "$(cat "$RUN/vendor.bluetooth-aidl-qti.before")" != running || start vendor.bluetooth-aidl-qti
fi
# Restart DNS resolver before allowing the next stock system_server connection.
if test "$(cat "$RUN/netd.before")" = running; then
    stop netd
    sleep 1
    start netd
    sleep 2
fi
for svc in vendor.qti.hardware.display.allocator vendor.qti.hardware.display.composer surfaceflinger zygote zygote_next zygote_third; do
    test "$(cat "$RUN/$svc.before")" = running || continue
    start "$svc" || failed=1
    n=0
    while test "$(getprop init.svc.$svc)" != running && test "$n" -lt 12; do
        sleep 1
        n=$((n+1))
    done
    test "$(getprop init.svc.$svc)" = running || failed=1
done
M=$S/magiskpolicy
if "$M" --load /sys/fs/selinux/policy --print-rules > "$RUN/restored.unsorted"; then
    "$B" sort "$RUN/restored.unsorted" > "$RUN/restored.rules"
    cmp "$RUN/baseline.rules" "$RUN/restored.rules" || failed=1
else
    failed=1
fi
# A running zygote alone does not establish that stock Android recovered.
n=0
while test "$n" -lt 60; do
    timeout -s KILL 3 service check activity > "$RUN/stock-activity.txt" 2>&1 || true
    timeout -s KILL 3 service check window > "$RUN/stock-window.txt" 2>&1 || true
    if "$B" grep -q ': found' "$RUN/stock-activity.txt" && "$B" grep -q ': found' "$RUN/stock-window.txt"; then
        echo stock-framework-services-present
        exit "$failed"
    fi
    sleep 1
    n=$((n+1))
done
echo stock-framework-recovery-timeout
exit 1
