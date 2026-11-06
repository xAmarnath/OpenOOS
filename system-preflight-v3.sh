#!/system/bin/sh
set -eu
umask 022
export ANDROID_DATA=/data ANDROID_ROOT=/system ASEC_MOUNTPOINT=/mnt/asec
export ANDROID_RUNTIME_ROOT=/apex/com.android.runtime ANDROID_ART_ROOT=/apex/com.android.art
export ANDROID_I18N_ROOT=/apex/com.android.i18n ANDROID_TZDATA_ROOT=/apex/com.android.tzdata
S=/data/local/tmp/gsi17-graphics-20261105-01
R=$S/rootfs
B=$S/busybox
RUN=$GSI_RUN
if test "${1:-}" != inside; then
  if test "${GSI_NETWORK_TEST:-0}" = 1; then
    exec "$B" nsenter -t "$(cat "$S/namespace.pid")" -m -- "$B" unshare -m /system/bin/sh "$S/system-preflight-v3.sh" inside
  fi
  exec "$B" nsenter -t "$(cat "$S/namespace.pid")" -m -- "$B" unshare -m -n /system/bin/sh "$S/system-preflight-v3.sh" inside
fi
test "$(readlink /proc/self/ns/mnt)" != "$(readlink /proc/1/ns/mnt)"
if test "${GSI_NETWORK_TEST:-0}" = 1; then
  test "$(readlink /proc/self/ns/net)" = "$(readlink /proc/1/ns/net)"
else
  test "$(readlink /proc/self/ns/net)" != "$(readlink /proc/1/ns/net)"
  ip link set lo up
fi
: > "$RUN/system-preflight.log"
"$B" mount --make-rprivate /
# Dedicated cgroup subtree holds guest processes only (kernel runtime state).
CG=/sys/fs/cgroup/gsi17-${RUN##*/}
mkdir -p "$CG"
"$B" mount --bind "$CG" "$R/sys/fs/cgroup"
mkdir -p "$R/sys/fs/cgroup/system" "$R/sys/fs/cgroup/apps"
# Android's system_server creates groups for isolated WebView processes itself.
# Delegate only this guest subtree, including the common ancestor migration file.
for dir in "$CG" "$CG/system" "$CG/apps"; do
    chown 1000:1000 "$dir"
    chmod 755 "$dir"
    for name in cgroup.procs cgroup.threads cgroup.subtree_control; do
        if test -e "$dir/$name"; then
            chown 1000:1000 "$dir/$name"
            chmod 664 "$dir/$name"
        fi
    done
done
for controller in cpuset cpuctl stune memcg blkio; do
    if test -d "$R/dev/$controller"; then
        "$B" mount --bind "$R/dev/$controller" "$R/dev/$controller"
        "$S/guest-remount-ro" "$R/dev/$controller"
    fi
done
"$B" mount -t proc -o nosuid,nodev,noexec proc "$R/proc"
"$B" mount --bind "$R/proc/sys" "$R/proc/sys"
"$S/guest-remount-ro" "$R/proc/sys"
if test "${GSI_NETWORK_TEST:-0}" = 1; then
    # Only live network sysctls; stock partition mounts remain read-only.
    "$B" mount --bind /proc/sys/net "$R/proc/sys/net"
fi
"$B" mount --bind /dev/null "$R/proc/sysrq-trigger"
"$B" mount --bind /sys/fs/selinux "$R/sys/fs/selinux"
# SELinux query APIs need writable pseudo-files; policy mutation stays masked.
for name in load enforce disable checkreqprot; do
    if test -e "$R/sys/fs/selinux/$name"; then
        "$B" mount --bind "$R/sys/fs/selinux/$name" "$R/sys/fs/selinux/$name"
        "$S/guest-remount-ro" "$R/sys/fs/selinux/$name"
    fi
done
"$B" mount -t binder binder "$R/dev/binderfs"
for name in binder hwbinder vndbinder; do
    if test ! -e "$R/dev/binderfs/$name"; then
        "$S/binder-device" "$R/dev/binderfs/binder-control" "$name"
    fi
    echo "$name host=$(stat -L -c %t:%T /dev/$name) guest=$(stat -L -c %t:%T "$R/dev/binderfs/$name")"
    test "$(stat -L -c %t:%T /dev/$name)" != "$(stat -L -c %t:%T "$R/dev/binderfs/$name")"
    chmod 666 "$R/dev/binderfs/$name"
done
# Dedicated guest image provides Android's required user xattrs.
test -f "$S/userdata-ext4.img" && test ! -L "$S/userdata-ext4.img"
"$B" mount -t ext4 -o loop,nosuid,nodev,noatime "$S/userdata-ext4.img" "$R/data"
. "$S/guest-storage-layout.sh"
if test -f "$S/gapps.enabled"; then
    . "$S/mount-gapps.sh"
fi
# Missing private GPU BPF map must use libmeminfo's unavailable-data path.
cp "$S/libmeminfo17-guest.so" "$R/data/local/tmp/libmeminfo17-guest.so"
chmod 644 "$R/data/local/tmp/libmeminfo17-guest.so"
chcon u:object_r:system_lib_file:s0 "$R/data/local/tmp/libmeminfo17-guest.so"
"$B" mount --bind "$R/data/local/tmp/libmeminfo17-guest.so" "$R/system/lib64/libmeminfo.so"
"$S/guest-remount-ro" "$R/system/lib64/libmeminfo.so"
# Vivo's sensors use data-array payloads for six standard sensor types.
for lib in libsensorservice libsensorserviceaidl; do
    cp "$S/$lib-vivo.so" "$R/data/local/tmp/$lib-vivo.so"
    chmod 644 "$R/data/local/tmp/$lib-vivo.so"
    chcon u:object_r:system_lib_file:s0 "$R/data/local/tmp/$lib-vivo.so"
    "$B" mount --bind "$R/data/local/tmp/$lib-vivo.so" "$R/system/lib64/$lib.so"
    "$S/guest-remount-ro" "$R/system/lib64/$lib.so"
done
"$B" mount -t tmpfs -o nosuid,nodev,size=1m tmpfs "$R/dev/socket"
chcon u:object_r:socket_device:s0 "$R/dev/socket"
for name in logd logdw logdr; do
    touch "$R/dev/socket/$name"
    "$B" mount --bind "/dev/socket/$name" "$R/dev/socket/$name"
done
# Private vendor configuration copy; original vendor mount stays read-only.
mkdir -p "$R/data/local/vendor-etc-base"
"$B" mount --bind /vendor/etc "$R/data/local/vendor-etc-base"
"$S/guest-remount-ro" "$R/data/local/vendor-etc-base"
"$B" mount -t tmpfs -o nosuid,nodev,size=4m tmpfs "$R/vendor/etc"
for file in /vendor/etc/*; do
    ln -s "/data/local/vendor-etc-base/${file##*/}" "$R/vendor/etc/${file##*/}"
    chcon -h u:object_r:vendor_configs_file:s0 "$R/vendor/etc/${file##*/}"
done
for name in public.libraries.txt task_profiles.json; do
    if test -f /vendor/etc/$name; then
        rm "$R/vendor/etc/$name"
        cp /vendor/etc/$name "$R/vendor/etc/$name"
    fi
done
cp /vendor/etc/media_profiles.xml "$R/vendor/etc/media_profiles_vendor.xml"
chmod 644 "$R/vendor/etc/media_profiles_vendor.xml"
rm "$R/vendor/etc/vintf"
cp -RL /vendor/etc/vintf "$R/vendor/etc/vintf"
rm "$R/vendor/etc/fstab.qcom"
printf "/dev/null /data tmpfs rw defaults\n" > "$R/vendor/etc/fstab.qcom"
for sku in sun kera tuna; do
    cp "$S/guest-manifest_$sku.xml" "$R/vendor/etc/vintf/manifest_$sku.xml"
done
chcon -R u:object_r:vendor_configs_file:s0 "$R/vendor/etc/vintf"
# Keep the copied power manifest: Android 17 requires IPower support info.
# The guest consumes the stock sensor HAL through the Binder bridge.
# Omit unavailable guest thermal HAL; stock thermal management remains running.
cat > "$R/vendor/etc/vintf/manifest/android.hardware.thermal-service.qti.xml" <<'VINTF'
<manifest version="1.0" type="device"></manifest>
VINTF
# Do not declare guest HALs that are not launched: waitForDeclaredService
# otherwise blocks framework boot indefinitely (confirmed by Java stacks).
for name in vendor.vivo.hardware.biometrics.fingerprint-service.xml face-default.xml c2_manifest_vendor.xml android.hardware.wifi.hostapd.xml android.hardware.usb-service.qti.xml android.hardware.security.keymint-service-qti.xml bluetooth_audio.xml bluetooth-finder.xml bt_lmp_event-saidl.xml; do
    printf '<manifest version="1.0" type="device"></manifest>\n' > "$R/vendor/etc/vintf/manifest/$name"
done
if test "${GSI_NETWORK_TEST:-0}" != 1; then
    for name in android.hardware.wifi.supplicant.xml android.hardware.wifi-service.xml; do
        printf '<manifest version="1.0" type="device"></manifest>\n' > "$R/vendor/etc/vintf/manifest/$name"
    done
fi
# Guest-only KeyMint endpoint. Do not declare stock shared-secret, Gatekeeper,
# or remote-provisioning instances: their device-wide state is not guest-owned.
cat > "$R/vendor/etc/vintf/manifest/guest-keymint.xml" <<'KEYMINT'
<manifest version="1.0" type="device">
  <hal format="aidl">
    <name>android.hardware.security.keymint</name>
    <version>3</version>
    <fqname>IKeyMintDevice/default</fqname>
  </hal>
  <hal format="aidl">
    <name>android.hardware.gatekeeper</name>
    <version>1</version>
    <fqname>IGatekeeper/default</fqname>
  </hal>
</manifest>
KEYMINT
chcon u:object_r:vendor_configs_file:s0 "$R/vendor/etc/vintf/manifest/guest-keymint.xml"
# Private permissions copy; GNSS and Bluetooth use the stock HAL bridge.
test -L "$R/vendor/etc/permissions"
rm "$R/vendor/etc/permissions"
cp -RL /vendor/etc/permissions "$R/vendor/etc/permissions"
cat > "$R/vendor/etc/permissions/guest-offline-features.xml" <<'FEATURES'
<permissions>
    <feature name="android.hardware.nfc" />
    <feature name="android.hardware.nfc.any" />
    <feature name="android.hardware.nfc.hce" />
</permissions>
FEATURES
chcon -R u:object_r:vendor_configs_file:s0 "$R/vendor/etc/permissions"
"$B" mount -o remount,ro "$R/vendor/etc"
# NFC's persistent flag is conditional on nfc.any. Saved userdata from before
# that feature was exposed can retain a parsed package without PERSISTENT,
# preventing automatic startup after its one-shot boot receiver disables itself.
# Invalidate only NFC parses on the mounted guest userdata; never follow links.
cache="$R/data/system/package_cache"
if test -d "$cache"; then
    test "$(readlink -f "$cache")" = "$cache"
    "$B" find "$cache" -maxdepth 2 -type f \( -name 'NfcNciApex@*' -o -name 'com.android.nfcservices.capex-*' \) -delete
fi
mkdir -p "$R/data/system/environ" "$R/data/local/tmp" "$R/data/dalvik-cache/arm64" "$R/data/misc/apexdata/com.android.art" "$R/data/resource-cache"
# Snapshot properties into private tmpfs before invoking any writer.
"$B" mount -t tmpfs -o nosuid,nodev,size=128m tmpfs "$R/dev/__properties__"
cp -a /dev/__properties__/. "$R/dev/__properties__/"
chcon u:object_r:properties_device:s0 "$R/dev/__properties__"
for file in "$R/dev/__properties__/"*; do
    name=${file##*/}
    case "$name" in
        u:object_r:*) chcon "$name" "$file" ;;
        property_info) chcon u:object_r:property_info:s0 "$file" ;;
        properties_serial) chcon u:object_r:properties_serial:s0 "$file" ;;
    esac
done
cp "$S/magisk" "$R/data/local/tmp/magisk"
chmod 755 "$R/data/local/tmp/magisk"
prop() { "$B" chroot "$R" /data/local/tmp/magisk resetprop -n "$@"; }
prop -f /system/build.prop
if test -f "$R/prop.default"; then prop -f /prop.default; fi
prop media.settings.xml /vendor/etc/media_profiles_vendor.xml
prop dalvik.vm.gctype CMC
# Match physical stock density at the native 1440x3168 display mode.
prop ro.sf.lcd_density 600
# Stock hides AOSP navigation for its own gesture system; guest needs its own bar.
prop qemu.hw.mainkeys 0
prop sys.boot_completed 0
prop bpf.progs_loaded 0
prop ro.crypto.state unencrypted
prop ro.crypto.type none
prop persist.vold.dont_commit_checkpoint 1
# Stock's FUSE BPF program is outside the guest. Both vold and MediaProvider
# must use ordinary FUSE; otherwise vold skips required Android/data binds
# while MediaProvider falls back to FUSE and rejects app-private paths.
prop ro.fuse.bpf.is_running false
prop ro.fuse.bpf.enabled false
prop persist.sys.fuse.bpf.override false
prop persist.sys.vold_app_data_isolation_enabled true
prop ro.frp.pst ""
prop ro.hw_timeout_multiplier 3
prop vendor.audio.hal.stubmode 1
prop ro.audio.ihaladaptervendorextension_enabled false
# No guest Bluetooth audio HAL is registered. Importing stock's enabled audio
# profiles makes AdapterService wait forever for it, blocking even BLE scans.
# Keep data/HID/GATT available; audio is intentionally outside this silent run.
for profile in a2dp.source a2dp.sink asha.central avrcp.target avrcp.controller bap.broadcast.assist bap.broadcast.source bap.unicast.client ccp.server csip.set_coordinator hap.client hfp.ag hfp.hf mcp.server vcp.controller; do
    prop "bluetooth.profile.$profile.enabled" false
done
prop persist.bluetooth.finder.supported false
prop ro.bluetooth.finder.supported false
# Unsupported mock selector makes the optional real sound-trigger factory
# fail immediately; Android catches it and keeps its built-in fake module.
prop debug.soundtrigger_middleware.use_mock_hal 1
prop apexd.status ready
prop ro.vendor.qspm.enable false
prop ro.cp_system_other_odex 0
prop ro.config.isolated_compilation_enabled false
prop persist.sys.usb.config none
prop sys.usb.config none
prop persist.adb.tls_server.enable 0
for name in fingerprint id display.id version.incremental version.release version.release_or_codename version.sdk version.sdk_full version.security_patch type tags; do
    value=$("$B" chroot "$R" /system/bin/getprop "ro.system.build.$name")
    test -z "$value" || prop "ro.build.$name" "$value"
done
# Device identity describes the physical phone; ro.build.* stays the real GSI build.
for name in brand device manufacturer model name; do
    value=$(getprop "ro.product.$name")
    test -z "$value" || prop "ro.product.$name" "$value"
done
"$B" chroot "$R" /system/bin/getprop ro.build.fingerprint
getprop ro.build.fingerprint
chcon u:object_r:apex_mnt_dir:s0 "$R/apex"
# Versioned mounts and container paths let guest apexd discover our read-only modules.
while read -r name version fs; do
    case "$name" in com.android.*) ;; *) exit 24;; esac
    target="$R/apex/$name@$version"
    if test -L "$target"; then rm "$target"; fi
    mkdir -p "$target"
    "$B" mount --bind "$R/apex/$name" "$target"
    "$S/guest-remount-ro" "$target"
    loop=$(awk -v f="$S/payloads/$name.img" '$2==f {print $1}' "$S/loops")
    test -n "$loop"
    container=/system/apex/$name.apex
    test ! -f "$R/system/apex/$name.capex" || container=/system/apex/$name.capex
    test -f "$R$container"
    printf '%s\n' "$container" > "$R/data/local/tmp/$name.backing"
    backing_target=$(readlink -f "$R/sys/block/${loop##*/}/loop/backing_file")
    case "$backing_target" in "$R/sys/"*) ;; *) exit 25;; esac
    "$B" mount --bind "$R/data/local/tmp/$name.backing" "$backing_target"
    "$S/guest-remount-ro" "$backing_target"
done < "$S/modules.list"
chmod 644 "$R/apex/apex-info-list.xml"
chcon u:object_r:apex_info_file:s0 "$R/apex/apex-info-list.xml"
touch "$R/data/local/tmp/guest-runtime-only"
cp "$S/zygote-launch" "$R/data/local/tmp/zygote-launch"
chmod 755 "$R/data/local/tmp/zygote-launch"
cp "$S/netd-launch" "$R/data/local/tmp/netd-launch"
chmod 755 "$R/data/local/tmp/netd-launch"
"$B" chroot "$R" /apex/com.android.sdkext/bin/derive_classpath /data/system/environ/classpath
while read -r action key value; do
    export "$key=$value"
done < "$R/data/system/environ/classpath"
cp "$S/guest-data-prepare.jar" "$R/data/local/tmp/guest-data-prepare.jar"
CLASSPATH=/data/local/tmp/guest-data-prepare.jar timeout -s KILL 25 "$S/guest-root" "$R" /system/bin/app_process64 /system/bin GuestDataPrepare > "$RUN/data-migration.log" 2>&1
# Populate guest-owned writable directories only.
for dir in system system_ce/0 system_de/0 misc misc_ce/0 misc_de/0 data user user_de/0 app app-private dalvik-cache resource-cache; do
    mkdir -p "$R/data/$dir"
    chown 1000:1000 "$R/data/$dir"
    chmod 771 "$R/data/$dir"
done
test ! -L "$R/data/user/0"
if test -d "$S/dalvik-cache09/arm64"; then cp -a "$S/dalvik-cache09/." "$R/data/dalvik-cache/"; fi
chcon u:object_r:system_data_file:s0 "$R/data"
for dir in system system_ce system_de misc misc_ce misc_de data user user_de app app-private dalvik-cache resource-cache; do
    chcon -Rh u:object_r:system_data_file:s0 "$R/data/$dir"
done
# Restore labels for APK resources, overlays and guest shared media.
"$S/guest-root" "$R" /system/bin/restorecon -RF /data/resource-cache /data/app /data/media /data/misc/profiles /data/dalvik-cache
# Bluetooth persists its native controller configuration outside app data.
mkdir -p "$R/data/misc/bluedroid" "$R/data/misc/bluetooth/logs"
chown -R 1002:1002 "$R/data/misc/bluedroid" "$R/data/misc/bluetooth"
chmod 770 "$R/data/misc/bluedroid" "$R/data/misc/bluetooth" "$R/data/misc/bluetooth/logs"
chcon -R u:object_r:bluetooth_data_file:s0 "$R/data/misc/bluedroid" "$R/data/misc/bluetooth"
for module in tethering wifi scheduling permission; do
    dir="$R/data/misc/apexdata/com.android.$module"
    mkdir -p "$dir"
    chown 0:1000 "$dir"
    chmod 771 "$dir"
    case "$module" in
        tethering) label=apex_tethering_data_file ;;
        wifi) label=apex_wifi_data_file ;;
        *) label=apex_system_server_data_file ;;
    esac
    chcon "u:object_r:$label:s0" "$dir"
done
if test "${GSI_NETWORK_TEST:-0}" = 1; then
    touch "$R/data/local/tmp/guest-shared-network"
    prop init.svc.wpa_supplicant stopped
    mkdir -p "$R/data/vendor/wifi/wpa/sockets" "$R/data/misc/wifi/sockets"
    mkdir -p "$R/data/vendor/log/wifi"
    chown -R 1010:1010 "$R/data/vendor/log"
    chown -R 1010:1010 "$R/data/vendor/wifi" "$R/data/misc/wifi"
    chmod 770 "$R/data/vendor/wifi" "$R/data/vendor/wifi/wpa" "$R/data/vendor/wifi/wpa/sockets"
    if test ! -f "$R/data/local/tmp/guest-wifi-imported"; then
        cp /data/misc/apexdata/com.android.wifi/WifiConfigStore.xml "$R/data/misc/apexdata/com.android.wifi/WifiConfigStore.xml"
        chown 1000:1000 "$R/data/misc/apexdata/com.android.wifi/WifiConfigStore.xml"
        chmod 600 "$R/data/misc/apexdata/com.android.wifi/WifiConfigStore.xml"
        touch "$R/data/local/tmp/guest-wifi-imported"
    fi
    "$S/guest-root" "$R" /system/bin/restorecon -RF /data/vendor/wifi /data/misc/wifi /data/misc/apexdata/com.android.wifi
fi
mkdir -p "$R/data/misc/apexdata/com.android.tethering/netstats"
chown 1000:1000 "$R/data/misc/apexdata/com.android.tethering/netstats"
chmod 700 "$R/data/misc/apexdata/com.android.tethering/netstats"
chcon u:object_r:apex_tethering_data_file:s0 "$R/data/misc/apexdata/com.android.tethering/netstats"
for dir in network_watchlist textclassifier keychain systemkeys ethernet appcompat prereboot connectivityblobdb surfaceflinger; do
    mkdir -p "$R/data/misc/$dir"
    chown 1000:1000 "$R/data/misc/$dir"
    chmod 770 "$R/data/misc/$dir"
done
for dir in users dropbox heapdump wallpapers; do
    mkdir -p "$R/data/system/$dir"
    chown 1000:1000 "$R/data/system/$dir"
    chmod 770 "$R/data/system/$dir"
    chcon u:object_r:system_data_file:s0 "$R/data/system/$dir"
done
mkdir -p "$R/mnt/expand" "$R/mnt/user/0" "$R/mnt/pass_through/0" "$R/mnt/installer/0" "$R/mnt/androidwritable/0" "$R/mnt/runtime/default" "$R/mnt/runtime/read" "$R/mnt/runtime/write" "$R/mnt/runtime/full" "$R/storage/emulated"
chmod 0755 "$R/mnt/installer" "$R/mnt/installer/0" "$R/mnt/androidwritable" "$R/mnt/androidwritable/0"
guest_root=$(stat -L -c %d:%i "$R")
host_root=$(stat -L -c %d:%i /)
test "$guest_root" != "$host_root"
# Cleanup is owned exclusively by the outer PID-namespace supervisor.
# Guest-only service label compatibility; underlying GSI stays read-only.
cp "$R/system/etc/selinux/plat_service_contexts" "$R/data/local/tmp/plat_service_contexts"
printf '\nandroid.security.guestcompat u:object_r:keystore_compat_hal_service:s0\n' >> "$R/data/local/tmp/plat_service_contexts"
sed -i 's/u:object_r:power_attribution_service:s0/u:object_r:powerstats_service:s0/g' "$R/data/local/tmp/plat_service_contexts"
sed -i 's/u:object_r:multiuser_compliance_logger_service:s0/u:object_r:user_service:s0/g' "$R/data/local/tmp/plat_service_contexts"
sed -i 's/u:object_r:memory_budget_service:s0/u:object_r:activity_service:s0/g' "$R/data/local/tmp/plat_service_contexts"
sed -i 's/u:object_r:keychain_manager_service:s0/u:object_r:activity_service:s0/g' "$R/data/local/tmp/plat_service_contexts"
sed -i 's/u:object_r:app_interaction_service:s0/u:object_r:activity_service:s0/g' "$R/data/local/tmp/plat_service_contexts"
sed -i 's/u:object_r:aiseal_system_internal_service:s0/u:object_r:activity_service:s0/g' "$R/data/local/tmp/plat_service_contexts"
sed -i 's/u:object_r:personal_context_service:s0/u:object_r:activity_service:s0/g' "$R/data/local/tmp/plat_service_contexts"
sed -i 's/u:object_r:app_lock_service:s0/u:object_r:activity_service:s0/g' "$R/data/local/tmp/plat_service_contexts"
sed -i 's/u:object_r:cross_device_trust_service:s0/u:object_r:activity_service:s0/g' "$R/data/local/tmp/plat_service_contexts"
sed -i 's/u:object_r:data_leak_prevention_service:s0/u:object_r:activity_service:s0/g' "$R/data/local/tmp/plat_service_contexts"
sed -i 's/u:object_r:clock_manager_service:s0/u:object_r:activity_service:s0/g' "$R/data/local/tmp/plat_service_contexts"
sed -i 's/u:object_r:hid_permission_service:s0/u:object_r:activity_service:s0/g' "$R/data/local/tmp/plat_service_contexts"
sed -i 's/u:object_r:safety_evaluator_service:s0/u:object_r:activity_service:s0/g' "$R/data/local/tmp/plat_service_contexts"
chmod 644 "$R/data/local/tmp/plat_service_contexts"
chcon "$(ls -Z /system/etc/selinux/plat_service_contexts | cut -d ' ' -f 1)" "$R/data/local/tmp/plat_service_contexts"
"$B" mount --bind "$R/data/local/tmp/plat_service_contexts" "$R/system/etc/selinux/plat_service_contexts"
"$S/guest-remount-ro" "$R/system/etc/selinux/plat_service_contexts"
: > "$RUN/guest-kmsg.log"
"$B" mount --bind "$RUN/guest-kmsg.log" "$R/dev/kmsg"
# Private metadata storage for this guest; never expose stock metadata.
"$B" mount -t tmpfs -o nosuid,nodev,size=64m tmpfs "$R/metadata"
chcon u:object_r:metadata_file:s0 "$R/metadata"
mkdir -p "$R/metadata/aconfig/flags" "$R/metadata/aconfig/maps" "$R/metadata/aconfig/boot" "$R/metadata/aconfig/telemetry"
chown -R 0:1000 "$R/metadata/aconfig"
chmod 775 "$R/metadata/aconfig" "$R/metadata/aconfig/maps" "$R/metadata/aconfig/boot" "$R/metadata/aconfig/telemetry"
chmod 770 "$R/metadata/aconfig/flags"
chcon -R u:object_r:aconfig_storage_metadata_file:s0 "$R/metadata/aconfig"
chcon u:object_r:aconfig_storage_flags_metadata_file:s0 "$R/metadata/aconfig/flags"
"$S/guest-root" "$R" /system/bin/aconfigd-system early-platform-init > "$RUN/aconfig-early.log" 2>&1 || echo aconfig_early_exit=$?
"$S/guest-root" "$R" /system/bin/aconfigd-system platform-init > "$RUN/aconfig-platform.log" 2>&1 || echo aconfig_platform_exit=$?
"$S/guest-root" "$R" /apex/com.android.configinfrastructure/bin/aconfigd-mainline bootstrap-init > "$RUN/aconfig-bootstrap.log" 2>&1 || echo aconfig_bootstrap_exit=$?
"$S/guest-root" "$R" /apex/com.android.configinfrastructure/bin/aconfigd-mainline init > "$RUN/aconfig-mainline.log" 2>&1 || echo aconfig_mainline_exit=$?
"$B" chroot "$R" /system/bin/servicemanager > "$RUN/guest-servicemanager.log" 2>&1 &
cp "$S/guest-props" "$R/data/local/tmp/guest-props"
chmod 755 "$R/data/local/tmp/guest-props"
"$S/guest-root" "$R" /data/local/tmp/guest-props > "$RUN/guest-props.log" 2>&1 &
sleep 2
chcon u:object_r:property_socket:s0 "$R/dev/socket/property_service"
# Guest BPF objects and network-only sysctl view. Global BPF knobs are already set.
"$B" mount -t bpf bpf "$R/sys/fs/bpf"
chmod 1777 "$R/sys/fs/bpf"
if test "${GSI_NETWORK_TEST:-0}" = 1; then
    # Existing kernel firewall chains reference Vivo's pinned BPF programs.
    mkdir -p "$R/sys/fs/bpf/vivo_net"
    "$B" mount --bind /sys/fs/bpf/vivo_net "$R/sys/fs/bpf/vivo_net"
    "$S/guest-remount-ro" "$R/sys/fs/bpf/vivo_net"
fi
"$B" mount -t tmpfs -o nosuid,nodev,size=1m tmpfs "$R/proc/sys/net/core"
for file in /proc/sys/net/core/*; do cat "$file" > "$R/proc/sys/net/core/${file##*/}"; done
printf '1\n' > "$R/proc/sys/net/core/bpf_jit_enable"
printf '1\n' > "$R/proc/sys/net/core/bpf_jit_kallsyms"
printf '0\n' > "$R/data/local/tmp/unprivileged_bpf_disabled"
"$B" mount --bind "$R/data/local/tmp/unprivileged_bpf_disabled" "$R/proc/sys/kernel/unprivileged_bpf_disabled"
# Optional platform tracing loaders are unnecessary for this offline guest.
for loader in /system/bin/bpfloader /apex/com.android.uprobestats/bin/uprobestatsbpfload; do
    if test -f "$R$loader"; then "$B" mount --bind "$S/guest-noop" "$R$loader"; fi
done
cp "$S/netbpfload17-guest" "$R/data/local/tmp/netbpfload17-guest"
chmod 755 "$R/data/local/tmp/netbpfload17-guest"
mkdir -p "$R/data/local/tmp/bpf"
cp -a "$R/apex/com.android.tethering/etc/bpf/." "$R/data/local/tmp/bpf/"
"$B" mount --bind "$R/data/local/tmp/netbpfload17-guest" "$R/apex/com.android.tethering/bin/netbpfload"
"$B" mount --bind "$R/data/local/tmp/bpf" "$R/apex/com.android.tethering/etc/bpf"
"$S/guest-root" "$R" /apex/com.android.tethering/bin/netbpfload > "$RUN/guest-bpf.log" 2>&1 || echo "guest_bpf_exit=$?"
test -d "$R/sys/fs/bpf/netd_shared/mainline_done"
prop bpf.progs_loaded 1
# Offline guest: skip cgroup BPF attachments; preserve maps and real netd services.
cp "$S/libnetd_updatable17-guest.so" "$R/data/local/tmp/libnetd_updatable17-guest.so"
chmod 644 "$R/data/local/tmp/libnetd_updatable17-guest.so"
"$B" mount --bind "$R/data/local/tmp/libnetd_updatable17-guest.so" "$R/apex/com.android.tethering/lib64/libnetd_updatable.so"
mkdir -p "$R/data/misc/net" "$R/data/misc/netd"
if test "${GSI_NETWORK_TEST:-0}" != 1; then
    "$S/guest-root" "$R" /data/local/tmp/netd-launch > "$RUN/guest-netd.log" 2>&1 &
fi
mkdir -p "$R/data/misc/apexdata/com.android.permission"
chown 1000:1000 "$R/data/misc/apexdata/com.android.permission"
chcon u:object_r:apex_system_server_data_file:s0 "$R/data/misc/apexdata/com.android.permission"
mkdir -p "$R/data/apex/active" "$R/data/apex/sessions" "$R/data/anr" "$R/data/misc/user" "$R/data/misc/installd" "$R/data/misc/profiles/cur/0" "$R/data/misc/profiles/ref"
"$B" mount -t tmpfs -o nosuid,nodev,size=1m tmpfs "$R/data_mirror"
for pair in user:data_ce user_de:data_de misc_ce:misc_ce misc_de:misc_de; do
    src=${pair%:*}; dst=${pair#*:}
    mkdir -p "$R/data_mirror/$dst/null"
    "$B" mount --rbind "$R/data/$src" "$R/data_mirror/$dst/null"
done
for pair in cur:cur_profiles ref:ref_profiles; do
    src=${pair%:*}; dst=${pair#*:}
    mkdir -p "$R/data_mirror/$dst"
    "$B" mount --bind "$R/data/misc/profiles/$src" "$R/data_mirror/$dst"
done
test "$(stat -c %d:%i "$R/data/data")" = "$(stat -c %d:%i "$R/data_mirror/data_ce/null/0")"
chown 1000:1000 "$R/data/anr"
chmod 0775 "$R/data/anr"
mkdir -p "$R/data/tombstones" "$R/data/misc/trace" "$R/data/misc/stats-data" "$R/data/misc/stats-service" "$R/data/misc/train-info"
cp "$S/guest-service-launch" "$R/data/local/tmp/guest-service-launch"
chmod 755 "$R/data/local/tmp/guest-service-launch"
"$S/guest-root" "$R" /data/local/tmp/guest-service-launch tombstoned > "$RUN/guest-tombstoned.log" 2>&1 &
"$S/guest-root" "$R" /data/local/tmp/guest-service-launch statsd > "$RUN/guest-statsd.log" 2>&1 &
test ! -e "$R/sys/module/lowmemorykiller/parameters/minfree"
"$S/guest-root" "$R" /data/local/tmp/guest-service-launch lmkd > "$RUN/guest-lmkd.log" 2>&1 &
"$S/guest-root" "$R" /system/bin/hwservicemanager > "$RUN/guest-hwservicemanager.log" 2>&1 &
"$S/guest-root" "$R" /system/bin/hw/android.system.suspend-service > "$RUN/guest-suspend.log" 2>&1 &
"$S/guest-root" "$R" /vendor/bin/hw/vendor.qti.hardware.memtrack-service > "$RUN/guest-memtrack.log" 2>&1 &
RICHTAP_DEVICE_PATH="/dev/vivo_haptic;/dev/vivo_haptic_vice" ENV_RICHTAP_CONFIG_PATH="/odm/etc/aac_richtap.config;/odm/etc/aac_richtap_1.config" "$S/guest-root" "$R" /vendor/bin/hw/vendor.vivo.hardware.vibrator.custom.service > "$RUN/guest-vibrator.log" 2>&1 &
# The shared-hardware run bridges stock ConsumerIr. Do not also register a
# second guest HAL for the same device/service before the bridge starts.
if test "${GSI_NETWORK_TEST:-0}" != 1; then
    "$S/guest-root" "$R" /vendor/bin/hw/android.hardware.ir-service.example > "$RUN/guest-ir.log" 2>&1 &
fi
"$S/guest-root" "$R" /vendor/bin/hw/android.hardware.power-service > "$RUN/guest-power.log" 2>&1 &
"$S/guest-root" "$R" /vendor/bin/hw/android.hardware.health-service.qti > "$RUN/guest-health.log" 2>&1 &
"$S/guest-root" "$R" /system/bin/installd > "$RUN/guest-installd.log" 2>&1 &
mkdir -p "$R/data/misc/vold" "$R/data/misc/keystore" "$R/data/media/0"
chown -R 1017:1017 "$R/data/misc/keystore"
chmod 700 "$R/data/misc/keystore"
chcon -R u:object_r:keystore_data_file:s0 "$R/data/misc/keystore"
cp "$S/libkm_compat-guest.so" "$R/data/local/tmp/libkm_compat-guest.so"
chmod 644 "$R/data/local/tmp/libkm_compat-guest.so"
chcon u:object_r:system_lib_file:s0 "$R/data/local/tmp/libkm_compat-guest.so"
"$B" mount --bind "$R/data/local/tmp/libkm_compat-guest.so" "$R/system/lib64/libkm_compat.so"
"$S/guest-remount-ro" "$R/system/lib64/libkm_compat.so"
cp "$S/guest-keystore-launch" "$R/data/local/tmp/guest-keystore-launch"
chmod 755 "$R/data/local/tmp/guest-keystore-launch"
cp "$S/libguest-compat.so" "$R/data/local/tmp/libguest-compat.so"
chmod 755 "$R/data/local/tmp/libguest-compat.so"
cp "$S/libboot_control_client17-guest.so" "$R/data/local/tmp/libboot_control_client17-guest.so"
chmod 644 "$R/data/local/tmp/libboot_control_client17-guest.so"
"$B" mount --bind "$R/data/local/tmp/libboot_control_client17-guest.so" "$R/system/lib64/libboot_control_client.so"
"$S/guest-remount-ro" "$R/system/lib64/libboot_control_client.so"
# Only this guest-owned tmpfs propagates mounts. Zygote makes its inherited
# view a slave, receiving vold's FUSE mounts without exporting app mounts.
test "$(stat -c %d "$R/mnt")" != "$(stat -c %d /mnt)"
"$B" mount --make-rshared "$R/mnt"
# Android init normally creates these slave views. Installer and writable
# processes bind them as /storage, so directories alone are insufficient.
for view in installer androidwritable; do
    "$B" mount --rbind "$R/mnt/user" "$R/mnt/$view"
    "$B" mount --make-rslave "$R/mnt/$view"
    "$B" mount --make-rshared "$R/mnt/$view"
done
"$S/guest-root" "$R" /system/bin/vold --blkid_context=u:r:blkid:s0 --blkid_untrusted_context=u:r:blkid_untrusted:s0 --fsck_context=u:r:fsck:s0 --fsck_untrusted_context=u:r:fsck_untrusted:s0 > "$RUN/guest-vold.log" 2>&1 &
# Run the actual GSI initialization before system_server can request an unlock.
timeout -s KILL 20 "$S/guest-root" "$R" /system/bin/vdc cryptfs init_user0 > "$RUN/guest-storage-check.log" 2>&1
"$S/guest-root" "$R" /system/bin/vold_prepare_subdirs prepare "" 0 3 >> "$RUN/guest-storage-check.log" 2>&1
# Vold owns all emulated-storage views; raw media binds bypass FUSE and leave
# apps unable to traverse media_rw directories.
"$S/guest-root" "$R" /system/bin/idmap2d > "$RUN/guest-idmap2d.log" 2>&1 &
"$S/guest-root" "$R" /system/bin/storaged > "$RUN/guest-storaged.log" 2>&1 &
"$S/guest-root" "$R" /apex/com.android.media.swcodec/bin/mediaswcodec > "$RUN/guest-mediaswcodec.log" 2>&1 &
# Provides media.codec and media.resource_manager for guest screen recording.
"$S/guest-root" "$R" /system/bin/mediaserver64 > "$RUN/guest-mediaserver.log" 2>&1 &
mkdir -p "$R/data/misc/gatekeeper" "$R/data/misc/adb"
chown 1000:1000 "$R/data/misc/gatekeeper"
chown 1000:2000 "$R/data/misc/adb"
chmod 700 "$R/data/misc/gatekeeper"
chmod 2750 "$R/data/misc/adb"
chcon u:object_r:gatekeeper_data_file:s0 "$R/data/misc/gatekeeper"
chcon u:object_r:adb_keys_file:s0 "$R/data/misc/adb"
cat > "$R/data/local/tmp/vendor_audio_interfaces.xml" <<'AUDIO'
<?xml version="1.0" encoding="UTF-8"?>
<libraries>
  <library name="audiohal-default" libraryName="libaudiocorehal.default.so" method="registerServices" mandatory="true"/>
  <library name="audioeffecthal" libraryName="libaudioeffecthal.qti.so" method="registerService" mandatory="true"/>
</libraries>
AUDIO
"$B" mount --bind "$R/data/local/tmp/vendor_audio_interfaces.xml" "$R/data/local/vendor-etc-base/audio/vendor_audio_interfaces.xml"
mkdir -p "$R/data/vendor/audio" "$R/data/vendor/audio_dsp" "$R/dev/socket/audioserver"
chmod 775 "$R/dev/socket/audioserver"
"$S/guest-root" "$R" /vendor/bin/hw/audiohalservice.qti > "$RUN/guest-audiohal.log" 2>&1 &
"$S/guest-root" "$R" /system/bin/audioserver > "$RUN/guest-audioserver.log" 2>&1 &
if test "${GSI_DISPLAY_TEST:-0}" = 1; then
    # Take the display only after guest storage/services pass preparation.
    touch "$RUN/host-changes"
    # Stock camera imports buffers through the stock allocator. It has no
    # display ownership; keep it available alongside the guest allocator.
    for svc in zygote zygote_next zygote_third surfaceflinger vendor.qti.hardware.display.composer; do
        test "$(cat "$RUN/$svc.before")" != running || stop "$svc"
    done
    sleep 3
    for svc in zygote surfaceflinger vendor.qti.hardware.display.composer; do
        test "$(getprop init.svc.$svc)" = stopped
    done
    if test "${GSI_NETWORK_TEST:-0}" = 1; then
        touch "$RUN/network-changes"
        stop wpa_supplicant
        stop netd
        sleep 1
        test "$(getprop init.svc.netd)" = stopped
        test "$(getprop init.svc.wpa_supplicant)" = stopped
    fi
    touch "$RUN/policy-changes"
    "$B" dd if="$RUN/framework.policy" of=/sys/fs/selinux/load bs=4194304
    # Keystore must start after its domain permissions are loaded.
"$S/guest-root" "$R" /system/bin/env LD_PRELOAD=/data/local/tmp/libguest-compat.so /system/bin/sleep 86400 > "$RUN/guest-compat-helper.log" 2>&1 &
compat_launcher=$!
n=0
until "$S/guest-root" "$R" /system/bin/service check android.security.guestcompat 2>/dev/null | grep -q ': found'; do
    n=$((n+1))
    if ! kill -0 "$compat_launcher" 2>/dev/null || test "$n" -ge 15; then
        echo 'Guest software key compatibility service failed readiness.' >&2
        exit 1
    fi
    sleep 1
done
# The bridge stays outside chroot, with independently checked guest/host Binder.
: > "$RUN/owned-key-bridge.log"
chown 1017:1017 "$RUN/owned-key-bridge.log"
chmod 600 "$RUN/owned-key-bridge.log"
chcon u:object_r:keystore_data_file:s0 "$RUN/owned-key-bridge.log"
"$S/owned-key-bridge" default > "$RUN/owned-key-bridge.log" 2>&1 &
keymint_launcher=$!
n=0
until "$S/guest-root" "$R" /system/bin/service check android.hardware.security.keymint.IKeyMintDevice/default 2>/dev/null | grep -q ': found'; do
    n=$((n+1))
    if ! kill -0 "$keymint_launcher" 2>/dev/null || test "$n" -ge 15; then
        echo 'Guarded guest hardware KeyMint failed readiness.' >&2
        exit 1
    fi
    sleep 1
done
# Only the isolated verifier may sign guest authentication tokens. The bridge
# creates its per-run HMAC key before this service starts. No stock verifier.
"$S/guest-gatekeeper" > "$RUN/guest-gatekeeper-hal.log" 2>&1 &
gatekeeper_launcher=$!
n=0
until "$S/guest-root" "$R" /system/bin/service check android.hardware.gatekeeper.IGatekeeper/default 2>/dev/null | grep -q ': found'; do
    n=$((n+1))
    if ! kill -0 "$gatekeeper_launcher" 2>/dev/null || test "$n" -ge 15; then
        echo 'Isolated guest credential service failed readiness.' >&2
        exit 1
    fi
    sleep 1
done
"$S/guest-root" "$R" /system/bin/gatekeeperd /data/misc/gatekeeper > "$RUN/guest-gatekeeper.log" 2>&1 &
"$S/guest-root" "$R" /data/local/tmp/guest-keystore-launch > "$RUN/guest-keystore.log" 2>&1 &
keystore_launcher=$!
n=0
until "$S/guest-root" "$R" /system/bin/service check android.system.keystore2.IKeystoreService/default 2>/dev/null | grep -q ': found'; do
    n=$((n+1))
    if ! kill -0 "$keystore_launcher" 2>/dev/null || test "$n" -ge 20; then
        cat "$R/data/misc/keystore/launcher.log" >> "$RUN/guest-keystore.log" 2>/dev/null || true
        echo 'Guest keystore failed readiness; refusing framework startup.' >&2
        exit 1
    fi
    sleep 1
done
    if test "${GSI_NETWORK_TEST:-0}" = 1; then
        # The controller retains client state when the old framework exits.
        # Reset only this runtime HAL before registering the guest callback.
        touch "$RUN/bluetooth-changes"
        stop vendor.bluetooth-aidl-qti
        sleep 1
        start vendor.bluetooth-aidl-qti
        n=0
        while test "$(getprop init.svc.vendor.bluetooth-aidl-qti)" != running && test "$n" -lt 10; do sleep 1; n=$((n+1)); done
        test "$(getprop init.svc.vendor.bluetooth-aidl-qti)" = running
        n=0
        until "$S/host-fd-clean" /system/bin/service check android.hardware.bluetooth.IBluetoothHci/default | grep -q ': found'; do
            n=$((n+1)); test "$n" -lt 10 || exit 1; sleep 1
        done
        "$S/bluetooth-bridge" "$R/dev/binderfs/binder" > "$RUN/bluetooth-bridge.log" 2>&1 &
        # Clear the previous NFC client before handing its callbacks to the guest.
        touch "$RUN/nfc-changes"
        stop vendor.nfc_hal_service_v2
        sleep 1
        start vendor.nfc_hal_service_v2
        n=0
        until "$S/host-fd-clean" "$B" timeout -s KILL 3 /system/bin/service check android.hardware.nfc.INfc/default | grep -q ': found'; do
            n=$((n+1)); test "$n" -lt 10 || exit 1; sleep 1
        done
        "$S/network-bridge" "$R/dev/binderfs/binder" > "$RUN/network-bridge.log" 2>&1 &
        "$S/guest-uid-network" watch "$RUN" > "$RUN/guest-uid-network.log" 2>&1 &
        "$S/hardware-bridge" "$R/dev/binderfs/binder" > "$RUN/hardware-bridge.log" 2>&1 &
        "$S/guest-root" "$R" /data/local/tmp/netd-launch --shared-network > "$RUN/guest-netd.log" 2>&1 &
    fi
    mkdir -p "$R/data/vendor/display" "$R/data/vendor/gpu" "$R/data/misc/display" "$R/data/vendor/gaming" "$R/data/vendor/vivoconfigstore"
    "$S/guest-root" "$R" /vendor/bin/vndservicemanager /dev/vndbinder > "$RUN/guest-vndservicemanager.log" 2>&1 &
    "$S/guest-root" "$R" /vendor/bin/vendor.qti.qspmhal-service > "$RUN/guest-qspm.log" 2>&1 &
    "$S/guest-root" "$R" /vendor/bin/hw/vendor.vivo.hardware.configstore@1.0-service > "$RUN/guest-configstore.log" 2>&1 &
    "$S/guest-root" "$R" /vendor/bin/hw/vendor.vivo.hardware.vcode@1.0-service > "$RUN/guest-vcode.log" 2>&1 &
    "$S/guest-root" "$R" /vendor/bin/hw/vendor.qti.hardware.display.allocator-service > "$RUN/guest-allocator.log" 2>&1 &
    "$S/guest-root" "$R" /vendor/bin/hw/vendor.qti.hardware.display.composer-service > "$RUN/guest-composer.log" 2>&1 &
    cp "$S/libcamera-binder-pool.so" "$R/data/local/tmp/libcamera-binder-pool.so"
    "$S/guest-root" "$R" /system/bin/env LD_PRELOAD=/data/local/tmp/libcamera-binder-pool.so /system/bin/cameraserver > "$RUN/guest-camera.log" 2>&1 &
    sleep 3
    "$S/guest-root" "$R" /system/bin/surfaceflinger > "$RUN/guest-sf.log" 2>&1 &
    # Wait for the service; keep normal HWC composition. The failed GPU-only
    # workaround did not address sux's inherited FDs above input_fence's limit.
    "$S/guest-root" "$R" /system/bin/sh -c '
        n=0
        until service check SurfaceFlinger | grep -q ": found"; do
            n=$((n+1)); test "$n" -lt 20 || exit 1; sleep 1
        done
    ' > "$RUN/guest-sf-readiness.log" 2>&1
fi
: > "$RUN/guest-apexd.log"
timeout -s KILL 5 "$B" chroot "$R" /system/bin/service list > "$RUN/guest-services.txt" 2>&1
# Cleanup only processes whose root is the guest image, never host root.
"$S/guest-root" "$R" /system/bin/logcat -b main -b system -b crash -v threadtime -T 1 > "$RUN/logcat.log" 2>&1 &
echo $$ > "$RUN/system-preflight.pid"
cp "$S/guest-ui-config.sh" "$R/data/local/tmp/guest-ui-config.sh"
cp "$S/guest-home-overlay.sh" "$R/data/local/tmp/guest-home-overlay.sh"
cp "$S/guest-home-overlay.jar" "$R/data/local/tmp/guest-home-overlay.jar"
cp "$S/guest-input-fix.jar" "$R/data/local/tmp/guest-input-fix.jar"
for file in guest-hardware.sh guest-brightness.jar; do
    cp "$S/$file" "$R/data/local/tmp/$file"
done
cp "$R/system/bin/app_process64" "$R/data/local/tmp/guest-app-process"
chmod 755 "$R/data/local/tmp/guest-app-process"
chcon u:object_r:system_file:s0 "$R/data/local/tmp/guest-app-process"
# Expose only the panel brightness runtime node; guest /sys stays read-only.
cat /sys/class/backlight/panel0-backlight/brightness > "$RUN/brightness.before"
touch "$R/data/local/tmp/panel-brightness"
"$B" mount --bind /sys/class/backlight/panel0-backlight/brightness "$R/data/local/tmp/panel-brightness"
"$B" mount -o remount,bind,rw "$R/data/local/tmp/panel-brightness"
"$S/guest-root" "$R" /system/bin/sh /data/local/tmp/guest-hardware.sh > "$RUN/guest-hardware.log" 2>&1 &
"$S/guest-root" "$R" /system/bin/sh -c 'until test "$(getprop sys.boot_completed)" = 1; do sleep 1; done; exec /system/bin/sh /data/local/tmp/guest-ui-config.sh' > "$RUN/ui-config.log" 2>&1 &
if test "${GSI_NETWORK_TEST:-0}" = 1; then
    "$S/guest-root" "$R" /system/bin/sh -c 'until test "$(getprop sys.boot_completed)" = 1; do sleep 1; done; sleep 5; cmd wifi set-wifi-enabled enabled' > "$RUN/wifi-enable.log" 2>&1 &
fi
zygote_status=0
"$S/guest-root" "$R" /data/local/tmp/zygote-launch system-server > "$RUN/system-preflight.log" 2>&1 || zygote_status=$?
printf '%s\n' "$zygote_status" > "$RUN/zygote.exit-code"
echo "zygote_exit=$zygote_status"
tail -n 50 "$RUN/system-preflight.log"
exit "$zygote_status"
