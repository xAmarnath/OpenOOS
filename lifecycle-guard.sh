#!/system/bin/sh
# Called from the host namespace before image replacement or removal.
set -eu
S=/data/local/tmp/gsi17-graphics-20261105-01
B=$S/busybox
test "$(readlink -f "$S")" = "$S"
test ! -L "$S"
test "$(readlink /proc/self/ns/mnt)" = "$(readlink /proc/1/ns/mnt)"
for events in /sys/fs/cgroup/gsi17-*/cgroup.events; do
    test -f "$events" || continue
    if grep -q '^populated 1$' "$events"; then echo "Guest processes remain: $events"; exit 3; fi
done
case "${1:-}" in
    data)
        for file in /sys/block/loop*/loop/backing_file; do
            read -r backing < "$file" || continue
            case "$backing" in "$S/userdata-ext4.img"|"${S#/data}/userdata-ext4.img")
                echo "Userdata is still attached: $file"; exit 4;;
            esac
        done;;
    all)
        for file in /proc/[0-9]*/mountinfo; do
            if "$B" grep -q " $S/rootfs\(/\| \)" "$file" 2>/dev/null; then
                echo "Guest mounts remain: $file"; exit 5
            fi
        done
        for file in /sys/block/loop*/loop/backing_file; do
            read -r backing < "$file" || continue
            case "$backing" in "$S"/*|"${S#/data}"/*)
                echo "Guest loop remains: $file"; exit 6;;
            esac
        done;;
    *) exit 2;;
esac
echo idle
