"""Export or install this firmware-specific guest, using only its data directory."""
from pathlib import Path, PurePosixPath
import argparse
import gzip
import hashlib
import json
import os
import shutil
import subprocess
import tarfile
from device import root, S, SERIAL
import gsi

HERE = Path(__file__).resolve().parent
ASSETS = ('system.img', 'payloads', 'gapps', 'gapps.enabled', 'userdata-seed.img',
          'modules.list', 'apex-info-list.xml', 'busybox', 'magisk', 'gbinder.conf')


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(4 * 1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def check_archive(path):
    seen = set()
    total = 0
    with tarfile.open(path, 'r|gz') as archive:
        for item in archive:
            name = item.name.rstrip('/')
            p = PurePosixPath(name)
            if (p.is_absolute() or '..' in p.parts or not p.parts or
                    '\\' in name or ':' in name or '\x00' in name or
                    p.parts[0] not in ASSETS or name in seen or
                    not (item.isfile() or item.isdir()) or item.size < 0):
                raise RuntimeError('Unsafe or duplicate archive entry: ' + item.name)
            seen.add(name)
            total += item.size
            if total > 12 * 1024**3:
                raise RuntimeError('Unexpected archive size')
    required = set(ASSETS)
    if not required <= seen:
        raise RuntimeError('Missing assets: ' + repr(required - seen))
    return total


def export(dest):
    gsi.check_device()
    dest = dest.resolve()
    if dest.exists():
        raise RuntimeError('Export destination must not exist: ' + str(dest))
    if shutil.disk_usage(dest.parent).free < 6 * 1024**3:
        raise RuntimeError('Need at least 6 GiB of PC free space')
    dest.mkdir()
    files = set(gsi.RUNTIME.values()) | {'gsi.py', 'device.py', 'guest_bundle.py', 'guest-device.json', 'hardware-status.txt'}
    for rel in sorted(files):
        target = dest / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(HERE / rel, target)
    archive = dest / 'guest-assets.tar.gz'
    partial = archive.with_suffix('.gz.partial')
    cmd = f'exec {S}/host-fd-clean {S}/busybox tar -c -C {S} ' + ' '.join(ASSETS)
    print('Exporting read-only images and prepared guest data...', flush=True)
    with (dest / 'export.log').open('wb') as err:
        proc = subprocess.Popen(['adb', '-s', SERIAL, 'exec-out',
                                 '/apex/com.android.virt/bin/sux', '-c', cmd], stdout=subprocess.PIPE, stderr=err)
        try:
            with gzip.open(partial, 'wb', compresslevel=1) as output:
                shutil.copyfileobj(proc.stdout, output, 4 * 1024 * 1024)
            if proc.wait(timeout=30):
                raise RuntimeError('Device asset export failed; see export.log')
        except BaseException:
            proc.kill()
            proc.wait()
            raise
    partial.rename(archive)
    print('Checking archive paths and checksums...', flush=True)
    unpacked = check_archive(archive)
    manifest = {rel: {'size': (dest / rel).stat().st_size, 'sha256': digest(dest / rel)}
                for rel in sorted(files | {archive.name})}
    (dest / 'bundle.json').write_text(json.dumps({'unpacked_bytes': unpacked, 'files': manifest}, indent=2))
    (dest / 'README.txt').write_text(
        'iQOO 13 / I2401 Android 17 guest\n\n'
        'Requires the matching stock firmware, Windows Python, adb, and temporary sux root.\n'
        'These commands run in this folder:\n'
        '  python guest_bundle.py verify\n'
        '  python guest_bundle.py install  (only if the guest directory is absent)\n'
        '  python gsi.py start            (no session time limit; stop restores stock)\n'
        '  python gsi.py status\n'
        '  python gsi.py stop\n'
        '  python gsi.py close\n'
        '  python gsi.py reset --execute  (restore the prepared guest snapshot after stop/close)\n'
        '  python gsi.py remove --execute (delete the guest directory after stop/close)\n\n'
        'If installation finished extracting but adb interrupted runtime deployment,\n'
        'reconnect adb and run python gsi.py setup, then python gsi.py start.\n\n'
        'A reboot clears temporary root and runtime mounts, but keeps guest data files.\n'
        'No boot hooks or stock partition installation. This bundle is tied to this phone/firmware.\n'
        'Prepared data can contain imported Wi-Fi configuration; keep this bundle private.\n'
        'Experimental hardware support: read hardware-status.txt for verified features and limitations.\n')
    print('Bundle created: ' + str(dest), flush=True)


def verify():
    manifest = json.loads((HERE / 'bundle.json').read_text())
    for rel, expected in manifest['files'].items():
        p = PurePosixPath(rel)
        if not p.parts or p.is_absolute() or '..' in p.parts or '\\' in rel or ':' in rel or '\x00' in rel:
            raise RuntimeError('Invalid manifest path')
        file = HERE / rel
        if file.stat().st_size != expected['size'] or digest(file) != expected['sha256']:
            raise RuntimeError('Bundle checksum failed: ' + rel)
    if check_archive(HERE / 'guest-assets.tar.gz') != manifest['unpacked_bytes']:
        raise RuntimeError('Unpacked size mismatch')
    print('All bundle checksums and archive paths verified.', flush=True)
    return manifest


def install():
    manifest = verify()
    gsi.check_device()
    # Exclusive, fixed staging location. Refuse to overwrite an existing guest.
    stage = S + '-install'
    archive = HERE / 'guest-assets.tar.gz'
    needed = manifest['unpacked_bytes'] + archive.stat().st_size + 4 * 1024**3
    free = int(gsi.command('df -k /data | tail -1').split()[3]) * 1024
    if free < needed:
        raise RuntimeError('Insufficient phone space')
    gsi.command(f'test ! -e {S}; test ! -L {S}; test ! -e {stage}; test ! -L {stage}; '
                f'test "$(readlink -f /data/local/tmp)" = /data/local/tmp; mkdir {stage}; chown 2000:2000 {stage}; chmod 700 {stage}')
    # Bootstrap executables have separate names from archive entries.
    for local, remote in ((HERE / 'host-fd-clean', stage + '/bootstrap-fd'),
                          (archive, stage + '/assets.tar.gz')):
        subprocess.run(['adb', '-s', SERIAL, 'push', str(local), remote], check=True, timeout=600)
    gsi.command(f'chown 0:0 {stage}; chmod 700 {stage}; test ! -L {stage}/assets.tar.gz; test ! -L {stage}/bootstrap-fd')
    remote_hash = gsi.command(f'sha256sum {stage}/assets.tar.gz', timeout=180).split()[0]
    if remote_hash != manifest['files']['guest-assets.tar.gz']['sha256']:
        raise RuntimeError('Uploaded archive checksum mismatch; staging retained at ' + stage)
    # Toybox tar is used only to retrieve the archive's validated BusyBox file.
    gsi.command(f'chmod 755 {stage}/bootstrap-fd; mkdir {stage}/bootstrap; '
                f'{stage}/bootstrap-fd /system/bin/tar -xzf {stage}/assets.tar.gz -C {stage}/bootstrap busybox; '
                f'chmod 755 {stage}/bootstrap/busybox', timeout=180)
    gsi.command(f'mkdir {stage}/payload; '
                f'{stage}/bootstrap-fd {stage}/bootstrap/busybox tar -xzf {stage}/assets.tar.gz -C {stage}/payload; '
                f'chmod 755 {stage}/payload/busybox {stage}/payload/magisk; '
                f'chcon -R u:object_r:system_file:s0 {stage}/payload/gapps; '
                f'cp {stage}/payload/userdata-seed.img {stage}/payload/userdata-ext4.img; '
                f'mkdir {stage}/payload/attempts {stage}/payload/rootfs {stage}/payload/namespaces; '
                f'sync; test ! -e {S}; mv {stage}/payload {S}', timeout=600)
    # Delete only the exact, checked temporary installer directory.
    gsi.command(f'test "$(readlink -f {stage})" = {stage}; test ! -L {stage}; rm -rf -- {stage}', timeout=120)
    gsi.deploy()
    print('Installed guest files. Run python gsi.py start when ready.', flush=True)


if __name__ == '__main__':
    os.chdir(HERE)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('export', 'verify', 'install'))
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with gsi.cli_lock():
        if args.action == 'export':
            if not args.output:
                parser.error('export requires --output')
            export(args.output)
        else:
            globals()[args.action]()
