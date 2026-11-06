#!/system/bin/sh
# S, R and B supplied by preflight in its private mount namespace.
test "$(readlink /proc/self/ns/mnt)" != "$(readlink /proc/1/ns/mnt)"
test -s "$S/gapps/GmsCore/base.apk"
base="$R/data/local/gsi-system-priv-app-base"
mkdir -p "$base"
"$B" mount --bind "$R/system/priv-app" "$base"
"$S/guest-remount-ro" "$base"
"$B" mount -t tmpfs -o nosuid,nodev,size=1m,mode=0755 tmpfs "$R/system/priv-app"
chcon u:object_r:system_file:s0 "$R/system/priv-app"
for source in "$base"/*; do
    name=${source##*/}
    if test -d "$source"; then
        mkdir "$R/system/priv-app/$name"
    else
        touch "$R/system/priv-app/$name"
    fi
    "$B" mount --bind "$source" "$R/system/priv-app/$name"
    "$S/guest-remount-ro" "$R/system/priv-app/$name"
done
for name in GmsCore Phonesky GoogleServicesFramework; do
    test ! -e "$R/system/priv-app/$name"
    mkdir "$R/system/priv-app/$name"
    "$B" mount --bind "$S/gapps/$name" "$R/system/priv-app/$name"
    "$S/guest-remount-ro" "$R/system/priv-app/$name"
done
# Fail before stopping stock if any original system app has been hidden.
for source in "$base"/*; do
    name=${source##*/}
    test -e "$R/system/priv-app/$name"
    for apk in "$source"/*.apk; do
        test ! -f "$apk" || test -s "$R/system/priv-app/$name/${apk##*/}"
    done
done
test -s "$R/system/priv-app/PackageInstaller/PackageInstaller.apk"
"$B" mount -o remount,ro "$R/system/priv-app"
permissions="$R/data/local/gsi-system-permissions-base"
mkdir -p "$permissions"
"$B" mount --bind "$R/system/etc/permissions" "$permissions"
"$S/guest-remount-ro" "$permissions"
"$B" mount -t tmpfs -o nosuid,nodev,size=4m tmpfs "$R/system/etc/permissions"
cp -a "$permissions/." "$R/system/etc/permissions/"
cp "$S/guest-gapps-permissions.xml" "$R/system/etc/permissions/guest-gapps-permissions.xml"
chmod 644 "$R/system/etc/permissions/guest-gapps-permissions.xml"
chcon -R u:object_r:system_file:s0 "$R/system/etc/permissions"
"$B" mount -o remount,ro "$R/system/etc/permissions"
