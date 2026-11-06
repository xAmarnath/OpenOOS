# On-device switcher

The app shows **Current OS**, the OS name, and one button to switch.
Install the same locally signed APK in stock and guest Android. The root
controller remains outside both Android app process groups and uses the existing
runtime supervisor to stop one framework and start the other.

## Build

Use JDK, Android SDK platform `android-37.0`, build-tools `37.0.0`, and Zig 0.13.0.
Keep the output and signing key outside the repository:

```powershell
python switcher/build.py --sdk $env:ANDROID_SDK_ROOT --out D:/openoos-build/switcher
./toolchain/zig-windows-x86_64-0.13.0/zig.exe cc -target aarch64-linux-musl -static -O2 -Wall -Wextra switcher/controller.c -o D:/openoos-build/switcher/controller
```

The first build creates a local signing key. Keep it private and reuse it for APK
updates. Its generated development password is not a distribution credential.
The controller source was cross-compiled successfully for ARM64; the minimal APK
was installed in both stock and guest Android. Both switch directions were exercised.

## Provision an existing runtime

This component expects the already prepared runtime described in
[Operations](OPERATIONS.md), at `/data/local/tmp/gsi17-graphics-20261105-01`.
It does not obtain root or download the GSI.

1. Install `OpenOOS-Switch.apk` with `pm install -r` in both Android environments.
2. Record each package UID using `cmd package list packages --user 0 -U org.openoos.switcher`
   in that environment. UIDs are different and must not be copied from another device.
3. In stock's mount namespace, create a root-owned `switcher` directory, mode `0700`,
   below the runtime. Copy `controller`, `transition.sh`, `start-controller.sh`, and
   `socket.rules` there. Set root ownership and mode `0700`.
4. Create root-owned mode `0600` files there: `uids` containing the stock UID then
   guest UID separated by a space, and `stock.fingerprint` containing stock's
   `getprop ro.build.fingerprint` output.
5. With stock running, execute `sh <runtime>/switcher/start-controller.sh` using
   temporary root in the host mount namespace. This adjusts only the live SELinux
   policy and starts the controller. It creates no boot service.

The current `guest-ui-config.sh` must be deployed with its completion marker;
`transition.sh` waits for that marker before opening the guest switcher. The app
shows unavailable when the controller is absent. After reboot, reestablish temporary
root and restart the controller using the prepared runtime.

## Control protocol

The Android app sends `STATUS`, `GUEST`, or `STOCK` to an abstract Unix socket.
The daemon checks the kernel-reported UID, app process name, and filesystem root.
It serializes transitions and rejects requests from the wrong environment.
The guest and stock share a kernel; these checks do not create VM isolation.

The controller uses `/data` runtime files, live policy, and service controls. It
contains no partition-flashing operation. Existing manual recovery remains available
through `python gsi.py stop` on the PC.
