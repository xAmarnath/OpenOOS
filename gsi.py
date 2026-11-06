"""iQOO 13 guest controls. Run from Windows with adb and temporary sux root."""
from pathlib import Path
import argparse
import json
import os
import re
import shlex
import struct
import time
from contextlib import contextmanager
from device import root, push, S

HERE = Path(__file__).resolve().parent
RUNTIME = {
    **{n: n for n in (
        'start-v3.sh', 'recover-v3.sh', 'worker-v3.sh', 'system-preflight-v3.sh',
        'namespace-v3.sh', 'start-namespace.sh', 'compile-framework-policy.sh',
        'lifecycle-guard.sh', 'framework.rules', 'guest-storage-layout.sh',
        'guest-ui-config.sh', 'guest-home-overlay.sh', 'guest-supervisor',
        'magiskpolicy', 'host-fd-clean', 'guest-root', 'guest-remount-ro', 'binder-device',
        'guest-props', 'guest-noop', 'zygote-launch', 'netd-launch',
        'guest-service-launch', 'guest-keystore-launch', 'libmeminfo17-guest.so', 'libkm_compat-guest.so',
        'libboot_control_client17-guest.so', 'libnetd_updatable17-guest.so',
        'netbpfload17-guest', 'guest-manifest_sun.xml', 'guest-manifest_kera.xml',
        'guest-manifest_tuna.xml')},
    **{n: 'network-port/' + n for n in ('hardware-bridge', 'network-bridge', 'bluetooth-bridge', 'guest-uid-network')},
    **{n: 'keymint-port/' + n for n in ('owned-key-bridge', 'guest-gatekeeper', 'libguest-compat.so')},
    **{n: 'hardware-port/' + n for n in (
        'libsensorservice-vivo.so', 'libsensorserviceaidl-vivo.so',
        'libcamera-binder-pool.so', 'guest-brightness.jar',
        'guest-data-prepare.jar',
        'guest-hardware.sh', 'mount-gapps.sh', 'guest-gapps-permissions.xml',
        'privapp-permissions-google-system.xml',
        'privapp-permissions-google-system_ext.xml',
        'privapp-permissions-google-product.xml')},
    'guest-home-overlay.jar': 'user-tests/guest-home-overlay.jar',
    'guest-input-fix.jar': 'user-tests/guest-input-fix.jar',
}

def say(text):
    print(text, flush=True)

def command(text, timeout=30):
    return root('set -eu\n' + text, timeout=timeout).decode().strip()


@contextmanager
def cli_lock():
    # Windows releases this lock even if Python is interrupted or crashes.
    import msvcrt
    with (HERE / 'guest-control.lock').open('a+b') as lock:
        lock.seek(0, 2)
        if not lock.tell():
            lock.write(b'0')
            lock.flush()
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as exc:
            raise RuntimeError('Another guest control command is running') from exc
        try:
            yield
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


def idle_command(text, timeout=30):
    # The same lock is held by the supervisor through guest recovery.
    body = 'set -eu\n' + text
    return command(f'{S}/host-fd-clean {S}/busybox flock -n {S}/attempts/supervisor.lock '
                   f'/system/bin/sh -c {shlex.quote(body)}', timeout)

def boot():
    return command('cat /proc/sys/kernel/random/boot_id')

def run_path():
    # Keep controls consistent when invoked from the workspace or an exported
    # bundle. The phone is authoritative; the local file supports older setups.
    remote = command(f'if test -f {S}/active-run; then '
                     f'test ! -L {S}/active-run && cat {S}/active-run; fi')
    path = HERE / 'active-run.txt'
    if not remote and not path.exists():
        return None
    value = remote or path.read_text().strip()
    if not re.fullmatch(re.escape(S) + r'/attempts/[a-z0-9-]+', value):
        raise RuntimeError('Invalid active run path')
    return value

def current():
    run = run_path()
    if not run:
        return None, 'none'
    info = command(f'if test -f {run}/boot.id; then cat {run}/boot.id; cat {run}/state 2>/dev/null || echo preparing; fi').splitlines()
    if not info or info[0] != boot():
        return run, 'previous-boot'
    return run, info[-1]

def require_idle():
    _, state = current()
    if state not in ('none', 'previous-boot', 'recovered'):
        raise RuntimeError('Stop and recover the guest first; state=' + state)
    command("for f in /sys/fs/cgroup/gsi17-*/cgroup.events; do "
            "test ! -f \"$f\" || ! grep -q '^populated 1$' \"$f\" || exit 3; done")

def check_device():
    values = command('id -u; getprop ro.product.model; getprop ro.build.fingerprint').splitlines()
    if len(values) != 3 or values[0] != '0' or values[1] != 'I2401':
        raise RuntimeError('Expected temporarily rooted I2401: ' + repr(values[:2]))
    compatibility = HERE / 'guest-device.json'
    if compatibility.exists() and json.loads(compatibility.read_text())['stock_fingerprint'] != values[2]:
        raise RuntimeError('Stock firmware changed. Rebuild and validate the hardware patches first.')
    if not compatibility.exists():
        compatibility.write_text(json.dumps({'model': values[1], 'stock_fingerprint': values[2]}, indent=2))
    return values

def upload_directory():
    path = S + '/uploads'
    # Install creates a root-private parent. adbd needs traversal permission to
    # reach its shell-owned upload directory; listing the parent stays denied.
    command(f'test "$(readlink -f {S})" = {S}; test ! -L {S}; chmod 711 {S}; '
            f'test ! -L {path}; mkdir -p {path}; chown 2000:2000 {path}; chmod 700 {path}')
    return path


def deploy():
    check_device()
    require_idle()
    for src in RUNTIME.values():
        if not (HERE / src).is_file():
            raise RuntimeError('Missing local runtime: ' + src)
    if b'gsi-props-20261106-wifi-aidl-v1' not in (HERE / 'guest-props').read_bytes():
        raise RuntimeError('Stale property helper: rebuild guest-props.c before deploying')
    command(f'test "$(readlink -f {S})" = {S}; test ! -L {S}; test -s {S}/system.img; test -s {S}/modules.list; test -s {S}/userdata-ext4.img; test -x {S}/busybox')
    upload = upload_directory() + '/runtime.tmp'
    for dst, src in RUNTIME.items():
        command(f'rm -f {upload}')
        push(HERE / src, upload)
        command(f'chown 0:0 {upload}; chmod 755 {upload}; mv {upload} {S}/{dst}')
    say('Runtime deployed. Original system/APEX images remain read-only.')

def namespace():
    say(command(f'sh {S}/start-namespace.sh'))
    for _ in range(45):
        state = command(f'cat {S}/namespace.state')
        if state == 'ready':
            command(f'test "$(cat {S}/namespace.boot)" = "$(cat /proc/sys/kernel/random/boot_id)"; '
                    f'p=$(cat {S}/namespace.pid); test "$(readlink /proc/$p/ns/mnt)" = "$(cat {S}/namespace.id)"; '
                    f'{S}/busybox nsenter -t "$p" -m -- test -f {S}/rootfs/system/build.prop')
            say('Namespace ready.')
            return
        if state == 'cleaned':
            raise RuntimeError(command(f'tail -n 30 {S}/namespace-v3.log'))
        time.sleep(1)
    raise RuntimeError('Namespace preparation timed out; inspect namespace-v3.log')

def fixed_policy(data, flags):
    result = bytearray(data)
    if not (24 < len(result) < 4194304) or result[8:16] != b'SE Linux':
        raise RuntimeError('Unexpected SELinux policy format')
    word = struct.unpack_from('<I', result, 20)[0]
    struct.pack_into('<I', result, 20, word | flags)
    return result

def start():
    check_device()
    require_idle()
    grow_userdata()
    namespace()
    upload = upload_directory() + '/policy.tmp'
    run = S + '/attempts/' + boot()[:8] + '-' + str(time.time_ns())
    command(f'mkdir {run}; cat /proc/sys/kernel/random/boot_id > {run}/boot.id; cat /sys/fs/selinux/policy > {run}/baseline.raw')
    # Reading the kernel policy may omit Vivo format flags; retain the stock header bits.
    header = root('head -c 24 /odm/etc/selinux/precompiled_sepolicy')
    if header[8:16] != b'SE Linux':
        raise RuntimeError('Unrecognized stock policy header')
    flags = struct.unpack_from('<I', header, 20)[0] & 0xc0000000
    say(command(f'sh {S}/compile-framework-policy.sh {run}'))
    cache = HERE / 'lifecycle' / run.split('/')[-1]
    cache.mkdir(parents=True)
    for name in ('baseline', 'framework'):
        target = cache / (name + '.policy')
        target.write_bytes(fixed_policy(root(f'cat {run}/{name}.raw'), flags))
        # adbd remains the unprivileged shell user; stage outside the root-owned run.
        command(f'rm -f {upload}')
        push(target, upload)
        command(f'chown 0:0 {upload}; chmod 600 {upload}; mv {upload} {run}/{target.name}')
    (HERE / 'active-run.txt').write_text(run)
    command(f'test ! -L {S}/active-run; test ! -L {S}/active-run.tmp; '
            f'printf "%s\\n" {shlex.quote(run)} > {S}/active-run.tmp; '
            f'mv {S}/active-run.tmp {S}/active-run')
    say(command(f'sh {S}/start-v3.sh {run}', timeout=45))
    say('Guest session has no time limit. Use stop to restore stock; failure recovery remains active.')


def grow_userdata():
    """Expand only the offline regular guest image; keep its pre-growth copy."""
    image = S + '/userdata-ext4.img'
    target = 32 * 1024**3
    size = int(command(f'test -f {image}; test ! -L {image}; stat -c %s {image}'))
    if size >= target:
        return
    free = int(command('df -k /data | tail -1').split()[3]) * 1024
    if free < target + size + 4 * 1024**3:
        raise RuntimeError('Need room for 32 GiB of guest data and its backup')
    backup = S + '/userdata-before-grow-' + str(time.time_ns()) + '.img'
    say('Backing up and expanding offline guest userdata to 32 GiB...')
    idle_command(f'sh {S}/lifecycle-guard.sh data; '
                 f'test -f {image}; test ! -L {image}; test ! -e {backup}; '
                 f'cp {image} {backup}; chmod 600 {backup}; sync; '
                 f'rc=0; /system/bin/e2fsck -pf {image} || rc=$?; '
                 f'test "$rc" -le 1; '
                 f'/system/bin/resize2fs {image} 32G; '
                 f'rc=0; /system/bin/e2fsck -pf {image} || rc=$?; '
                 f'test "$rc" -le 1; sync; '
                 f'test "$(stat -c %s {image})" -ge {target}', timeout=240)
    say('Guest image expanded to 32 GiB; previous data image retained: ' + backup)

def stop():
    run, state = current()
    if state in ('none', 'previous-boot', 'recovered'):
        say('Guest already stopped; ' + state)
        return
    command(f'touch {run}/recover.request')
    say('Stock restoration requested.')
    for i in range(150):
        state = command(f'cat {run}/state')
        if state == 'recovered':
            say('Stock recovered; guest cgroup drained and runtime policy restored.')
            return
        if state == 'recovery-failed':
            raise RuntimeError('Recovery failed; inspect ' + run + '/supervisor.log')
        if i and i % 20 == 0:
            say(state)
        time.sleep(1)
    raise RuntimeError('Recovery still pending; inspect status before starting again')

def close_namespace():
    require_idle()
    command(f'touch {S}/close-namespace {S}/close-extended-namespace')
    for _ in range(15):
        state = command(f'cat {S}/namespace.state 2>/dev/null || true')
        if state == 'cleaned':
            break
        time.sleep(1)
    say(command(f'sh {S}/lifecycle-guard.sh all'))

def seed():
    require_idle()
    idle_command(f'sh {S}/lifecycle-guard.sh data; '
            f'test ! -e {S}/userdata-seed.img; test ! -L {S}/userdata-ext4.img; '
            f'cp {S}/userdata-ext4.img {S}/userdata-seed.img.partial && '
            f'sync && mv {S}/userdata-seed.img.partial {S}/userdata-seed.img', timeout=180)
    say('Prepared guest data saved. This is a setup snapshot, not an empty Android factory image.')

def reset(execute):
    if not execute:
        say('Preview: replace only ' + S + '/userdata-ext4.img with userdata-seed.img. Add --execute to apply.')
        return
    require_idle()
    idle_command(f'sh {S}/lifecycle-guard.sh data; '
            f'test -s {S}/userdata-seed.img; test ! -L {S}/userdata-seed.img; '
            f'test ! -L {S}/userdata-ext4.img; '
            f'test ! -e {S}/userdata-reset.img; '
            f'cp {S}/userdata-seed.img {S}/userdata-reset.img && sync && '
            f'mv {S}/userdata-reset.img {S}/userdata-ext4.img', timeout=180)
    say('Guest data restored to its prepared snapshot.')

def remove(execute):
    say('Target: ' + S + ' (guest files, apps, images and logs)')
    if not execute:
        say('Preview only. Run stop, close, then remove --execute to delete this exact directory.')
        return
    require_idle()
    idle_command(f'sh {S}/lifecycle-guard.sh all; '
                 f'test "$(readlink -f {S})" = {S} && test ! -L {S} && rm -rf -- {S}', timeout=180)
    say('Guest directory removed. PC workspace retained.')

def status():
    run, state = current()
    say(json.dumps({'run': run, 'state': state, 'boot': boot()}, indent=2))
    if run and state != 'previous-boot':
        say(command(f'cat {run}/zygote.exit-code 2>/dev/null || true; tail -n 5 {run}/supervisor.log 2>/dev/null || true'))

def main():
    os.chdir(HERE)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('setup', 'start', 'stop', 'status', 'close', 'seed', 'reset', 'remove'))
    parser.add_argument('--execute', action='store_true', help='Apply a reset or removal; otherwise preview only')
    args = parser.parse_args()
    if args.action == 'setup':
        deploy()
        namespace()
    elif args.action in ('reset', 'remove'):
        globals()[args.action](args.execute)
    elif args.action == 'close':
        close_namespace()
    else:
        globals()[args.action]()

if __name__ == '__main__':
    with cli_lock():
        main()
