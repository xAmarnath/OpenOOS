#!/system/bin/sh
set -eu
S=/data/local/tmp/gsi17-graphics-20261105-01
D=$S/switcher
test "$(id -u)" = 0
test "$(readlink /proc/self/ns/mnt)" = "$(readlink /proc/1/ns/mnt)"
test "$(getprop init.svc.zygote)" = running
test "$(getprop ro.build.fingerprint)" = "$(cat "$D/stock.fingerprint")"
# Apply to runtime policy only, before baseline capture by the next guest run.
cat /sys/fs/selinux/policy > "$D/socket-baseline.raw"
"$S/magiskpolicy" --load "$D/socket-baseline.raw" --save "$D/socket.raw" --apply "$D/socket.rules"
"$D/controller" --patch "$D/socket.raw" "$D/socket.policy"
dd if="$D/socket.policy" of=/sys/fs/selinux/load bs=4194304
setsid "$S/host-fd-clean" "$D/controller" --daemon </dev/null > "$D/controller.log" 2>&1 &
sleep 1
"$D/controller" --request STATUS
