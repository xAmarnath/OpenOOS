#!/system/bin/sh
# S, R and B are supplied by the isolated guest mount owner.
test "$(readlink /proc/self/ns/mnt)" != "$(readlink /proc/1/ns/mnt)"
test "$(stat -c %d "$R/data")" != "$(stat -c %d /data)"
for dir in vendor_ce vendor_de storage_area; do
    mkdir -p "$R/data/$dir"
    chown 0:0 "$R/data/$dir"
    chmod 0551 "$R/data/$dir"
done
for dir in singleton_ce singleton_de user user_de; do
    mkdir -p "$R/data/$dir"
    chown 1000:1000 "$R/data/$dir"
    chmod 0511 "$R/data/$dir"
done
for dir in misc_ce misc_de; do
    mkdir -p "$R/data/$dir"
    chown 1000:9998 "$R/data/$dir"
    chmod 0551 "$R/data/$dir"
done
for dir in system_ce system_de; do
    mkdir -p "$R/data/$dir"
    chown 1000:1000 "$R/data/$dir"
    chmod 0550 "$R/data/$dir"
done
mkdir -p "$R/data/data" "$R/data/media" "$R/data/system/users" "$R/data/misc/profiles/cur"
# CE parents are needed by the vendor's vold_prepare_subdirs before unlock.
for dir in vendor_ce/0 singleton_ce/0 system_ce/0 misc_ce/0; do
    mkdir -p "$R/data/$dir"
done
chown 1000:1000 "$R/data/data"
chmod 0771 "$R/data/data"
chown 1023:1023 "$R/data/media"
chmod 0550 "$R/data/media"
# Replace only the known old alias; never remove a real data directory.
if test -L "$R/data/user/0"; then
    test "$(readlink "$R/data/user/0")" = ../data
    rm "$R/data/user/0"
fi
mkdir -p "$R/data/user/0"
"$B" mount --bind "$R/data/data" "$R/data/user/0"
test "$(stat -c %d:%i "$R/data/data")" = "$(stat -c %d:%i "$R/data/user/0")"
