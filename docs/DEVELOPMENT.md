# Development snapshot

## What is available

The repository contains selected current controller scripts, native helper
sources, Binder bridges, credential service source, and hardware helpers.
Historical logs, downloaded source trees,
toolchains, APKs, signing keys, images, and session artifacts are excluded.

This snapshot preserves the current experimental implementation. There is no
complete, verified build-from-scratch pipeline yet. The existing scripts below
are components of that workflow, not a claim that these commands alone recreate
the entire runnable setup.

## Toolchain and dependencies

- Windows Python 3.10+ and Android Platform Tools for the controller.
- Zig 0.13.0 at `toolchain/zig-windows-x86_64-0.13.0/zig.exe` for existing native build scripts.
- JDK plus Android SDK platform `android-37.0` and build-tools `37.0.0` for Java helpers.
  Set `ANDROID_SDK_ROOT`; the published scripts do not contain a personal SDK path.
- `libgbinder`, `libglibutil`, and an Alpine aarch64 development sysroot under `network-port/`.
  Fetch scripts also expect `network-port/APKINDEX` from Alpine v3.22.
- AOSP Gatekeeper headers/core source and OpenSSL development libraries for the guest verifier.
- `pyelftools` and `capstone` for the sensor patch script.
- The exact GSI/stock input libraries for binary compatibility patches.

Some historical fetch scripts follow upstream `main`/`master` and some dependency
preparation steps are still manual. Pinning source revisions, checksums, extraction,
and complete build recipes is pending. Do not treat the runtime reference hashes
as hashes of downloadable source dependencies.

## Existing component builds

Run from the repository root, after preparing the relevant dependencies:

```powershell
python network-port/build-network.py
python network-port/build-hardware.py
python network-port/build-bluetooth.py
python keymint-port/build.py owned-key-bridge
python keymint-port/build.py guest-gatekeeper
python keymint-port/build-compat.py
python hardware-port/build-java.py hardware-port/GuestBrightness.java hardware-port/guest-brightness.jar
python hardware-port/build-java.py hardware-port/GuestDataPrepare.java hardware-port/guest-data-prepare.jar
New-Item -ItemType Directory -Force user-tests | Out-Null
python hardware-port/build-java.py GuestHomeOverlay.java user-tests/guest-home-overlay.jar
```

Original binaries and build products stay out
of Git. The controller maps local runtime paths to device names in `gsi.py:RUNTIME`.
The [runtime inventory](runtime-inventory.json) covers 52 current runtime inputs;
several require external files or additional build recipes.

## Recent changes preserved in this source

- Close inherited descriptors before guest launch. The display investigation
  found descriptor values beyond the driver's accepted input-fence range.
- Restrict shared-mount propagation to the guest's own `/mnt` tree so shared
  storage mounts can reach guest applications.
- Prepare guest user data and labels and grow the offline userdata image to 32 GiB.
- Track guest-created keys and run a separate guest credential verifier.
- Encode absent KeyMint application ID/data as empty AIDL byte vectors instead
  of null. Hardware-backed key generation and app attestation now work.
- Give the iQOO 13 status bar 24 dp side padding and 4 dp top padding through
  a guest SystemUI overlay. A brief font-scale refresh applies it to the running
  view, then restores the startup script's 1.0 scale. The live portrait capture
  confirmed the clock moved from about 15 px to 90 px from the left edge at
  600 dpi, without restarting the guest. Landscape remains unverified.

Hardware-backed key generation, app attestation, storage/download saving, and
Google sign-in work. Screen-lock setup remains a separate unresolved item.
Hardware-backed operations and the software guest credential verifier coexist
in the current bridge. See [the switcher build](SWITCHER.md) for on-device controls.

## Local source check

```powershell
python tools/verify-source.py
```

The checker verifies the published file allowlist, hashes, text formats, Python
syntax, and optional shell syntax when Bash is installed. It does not connect to
the phone, execute imported controller modules, build native code, or verify a
guest boot. When editing source, update `source-manifest.json` after reviewing
the intended changes.
