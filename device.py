"""ADB transport for a device explicitly selected by the operator."""
import os
import re
import subprocess

SERIAL = os.environ.get('OPENOOS_SERIAL') or os.environ.get('ANDROID_SERIAL', '')
# This path is compiled into the native helpers too. Changing it requires a rebuild.
S = '/data/local/tmp/gsi17-graphics-20261105-01'


def require_serial():
    if not SERIAL or not re.fullmatch(r'[A-Za-z0-9_.:\-]+', SERIAL):
        raise RuntimeError('Set OPENOOS_SERIAL to the device identifier shown by adb devices.')
    return SERIAL


def root(cmd, timeout=30):
    wrapped = '( ' + cmd + '\n); rc=$?; printf "\\n__GSI_RC_%d\\n" "$rc"'
    result = subprocess.run(
        ['adb', '-s', require_serial(), 'exec-out', '/apex/com.android.virt/bin/sux', '-c', wrapped],
        capture_output=True, timeout=timeout)
    data, separator, code = result.stdout.rpartition(b'\n__GSI_RC_')
    if result.returncode or not separator or code.strip() != b'0':
        raise RuntimeError((result.stdout + result.stderr).decode(errors='replace'))
    return data


def push(local, remote):
    subprocess.run(['adb', '-s', require_serial(), 'push', str(local), remote],
                   check=True, capture_output=True, timeout=45)
