#!/system/bin/sh
set -eu
S=/data/local/tmp/gsi17-graphics-20261105-01
B=$S/busybox
RUN=$GSI_RUN
test "$(cat "$RUN/boot.id")" = "$(cat /proc/sys/kernel/random/boot_id)"
"$B" grep -q "0::/gsi17-${RUN##*/}$" /proc/self/cgroup
export GSI_DISPLAY_TEST=1 GSI_NETWORK_TEST=1
exec /system/bin/sh "$S/system-preflight-v3.sh"
