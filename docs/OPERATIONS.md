# Session, storage, and recovery

These commands require an already prepared runtime on the supported firmware.
They are not a fresh-install recipe for the source checkout.

## Select the device

```powershell
adb devices
$env:OPENOOS_SERIAL = 'YOUR_ADB_DEVICE_ID'
```

`ANDROID_SERIAL` is also accepted. No personal device identifier is embedded in
the public transport module. The guest path is deliberately fixed at
`/data/local/tmp/gsi17-graphics-20261105-01` in both scripts and native helpers.
Changing only the Python constant will break the runtime.

## Run and stop

```powershell
python gsi.py setup   # deploy matching runtime files while idle; prepare mounts
python gsi.py start   # hand display/framework control to the guest
python gsi.py status
python gsi.py stop    # request recovery and wait for stock services
python gsi.py close   # release idle guest mounts and loop devices
```

Stop before updating runtime files. Keep a working ADB connection for session
control. Recovery can fail; if the phone requires a normal reboot, this setup
has no boot hook that starts the guest automatically.

## Persistence

Guest app data lives in `userdata-ext4.img`. The controller expands an offline
image to 32 GiB when necessary, after checking free space and keeping a backup.
Additional images and backups consume real space on stock userdata, even though
stock system/boot/vendor partitions are not flashed. A reboot clears temporary
root and active runtime mounts; it **does not delete these files**.

The runtime can import stock Wi-Fi settings into guest data on first shared-network
startup. A guest snapshot must therefore be treated as private, even if no Google
account has been added. Hardware key operations may also retain state in the
vendor security backend; filesystem isolation alone is not a promise that every
hardware operation is erased on reboot.

## Reset or remove

After successful `stop` and `close`:

```powershell
python gsi.py reset             # preview target
python gsi.py reset --execute   # overwrite guest userdata with the prepared seed
python gsi.py remove            # preview target
python gsi.py remove --execute  # delete the exact guest directory
```

Reset destroys current guest app data and restores the prepared seed. The seed
is a saved setup snapshot, not necessarily empty Android data. Remove deletes
guest files, images, and logs on the phone; the PC checkout remains.

## Private backup bundles

`guest_bundle.py export --output PATH` is intended for the owner's local backup.
It collects existing assets and a prepared data seed. It is **not a public release
exporter** and does not sanitize accounts, Wi-Fi settings, or guest data.
Its verifier/installer depends on the complete matching bundle and compiled
bootstrap tools. The old development bundle predates the latest fixes.
