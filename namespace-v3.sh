#!/system/bin/sh
# Only mounts below R in a new, recursively private mount namespace.
# No stock partition writes, host data bridges, service stops or SELinux changes.
set -eu
S=/data/local/tmp/gsi17-graphics-20261105-01
R=$S/rootfs
B=$S/busybox
PATH=/system/bin
export PATH
test "$(readlink /proc/self/ns/mnt)" != "$(readlink /proc/1/ns/mnt)"
$B mount --make-rprivate /
echo "namespace=$(readlink /proc/self/ns/mnt) host=$(readlink /proc/1/ns/mnt)"
test "$(readlink -f "$S")" = "$S"
test ! -L "$R"
mkdir -p "$R"
BOOT=$(cat /proc/sys/kernel/random/boot_id)
N=$S/namespaces/$BOOT-$$
mkdir -p "$N"
: > "$N/loops"
cleanup() {
    trap - EXIT INT TERM HUP
    echo "Namespace cleanup"
    $B umount -l "$R" 2>/dev/null || true
    while read -r dev file; do
        # Detach only our loop devices, after checking their backing file.
        case "$dev" in /dev/block/loop[0-9]*|/dev/loop[0-9]*) ;; *) continue;; esac
        name=${dev##*/}
        backing=$(cat "/sys/block/$name/loop/backing_file" 2>/dev/null || true)
        # sysfs reports paths relative to the backing filesystem (/data).
        case "$backing" in "$S"/*|"${S#/data}"/*) losetup -d "$dev" || true;; esac
    done < "$N/loops"
    if test "$(cat "$S/namespace.pid" 2>/dev/null)" = "$$"; then
        echo cleaned > "$S/namespace.state"
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
mount_image() {
    file=$1 dest=$2 fs=$3
    case "$file" in "$S/system.img"|"$S/payloads/"*.img) ;; *) exit 21;; esac
    case "$dest" in "$R"|"$R/apex/"*) ;; *) exit 22;; esac
    test -f "$file" && test ! -L "$file"
    # BusyBox supports long backing paths; Toybox rejects them after attaching.
    dev=$($B losetup -f)
    n=0
    while test ! -b "$dev" && test "$n" -lt 5; do sleep 1; n=$((n+1)); done
    test -b "$dev"
    echo "$dev $file" >> "$N/loops"
    $B losetup -r "$dev" "$file"
    test "$(cat /sys/block/${dev##*/}/ro)" = 1
    opts=ro,nosuid,nodev,noatime
    test "$fs" != ext4 || opts=$opts,noload
    $B mount -t "$fs" -o "$opts" "$dev" "$dest"
}
bind_ro() {
    src=$1 dest=$2
    case "$dest" in "$R/"*) ;; *) exit 23;; esac
    $B mount --bind "$src" "$dest"
    $B mount -o remount,bind,ro "$dest"
}
mount_image "$S/system.img" "$R" ext4
for d in data apex linkerconfig mnt storage tmp; do
    $B mount -t tmpfs -o nosuid,nodev,size=256m tmpfs "$R/$d"
done
# These are private guest tmpfs paths, never stock /data.
mkdir -p "$R/data/misc" "$R/data/local/tmp" "$R/data/vendor" "$R/data/misc/apexdata"
$B mount --rbind /dev "$R/dev"
# Mask raw block devices in the guest's /dev view.
$B mount -t tmpfs -o ro,nosuid,nodev,noexec,size=4k tmpfs "$R/dev/block"
if test -e "$R/dev/loop-control"; then bind_ro /dev/null "$R/dev/loop-control"; fi
bind_ro /dev/null "$R/dev/socket/property_service"
bind_ro /proc "$R/proc"
bind_ro /sys "$R/sys"
bind_ro /vendor "$R/vendor"
if test -d /odm; then bind_ro /odm "$R/odm"; fi
# Only mount the GSI's APEX payloads. No stock apexd activation/install.
while read -r name version fs; do
    case "$name" in com.android.*) ;; *) exit 24;; esac
    mkdir "$R/apex/$name"
    mount_image "$S/payloads/$name.img" "$R/apex/$name" "$fs"
    ln -s "$name" "$R/apex/$name@$version"
done < "$S/modules.list"
cp "$S/apex-info-list.xml" "$R/apex/apex-info-list.xml"
# Bootstrap with a private copy, then generate linker configuration from guest files.
cp -R /linkerconfig/. "$R/linkerconfig/"
echo "Generating guest linker configuration"
$B chroot "$R" /apex/com.android.runtime/bin/linkerconfig --target /linkerconfig > "$S/linkerconfig.log" 2>&1
# App processes must read the generated APEX JNI library map under enforcing
# SELinux. A tmpfs label makes libnativeloader silently skip that map.
chcon -R u:object_r:linkerconfig_file:s0 "$R/linkerconfig"
chmod 755 "$R/linkerconfig"
echo "Checking guest native loading without running SurfaceFlinger"
$B chroot "$R" /apex/com.android.runtime/bin/linker64 --list /system/bin/surfaceflinger > "$S/sf-libraries.txt" 2>&1
$B chroot "$R" /apex/com.android.runtime/bin/linker64 --list /system/bin/bootanimation > "$S/animation-libraries.txt" 2>&1
cat /proc/self/mountinfo > "$S/guest-mountinfo.txt"
echo $$ > "$S/namespace.pid"
readlink /proc/self/ns/mnt > "$S/namespace.id"
printf '%s\n' "$BOOT" > "$S/namespace.boot"
cp "$N/loops" "$S/loops"
echo ready > "$S/namespace.state"
echo "Guest prepared; no Android services launched."
# Keep the prepared mounts until explicitly closed.
while test ! -f "$S/close-namespace"; do
    sleep 1
done
