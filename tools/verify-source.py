"""Check a reviewed source snapshot locally; never invoke ADB or guest code."""
from pathlib import Path, PurePosixPath
import ast
import hashlib
import json
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'source-manifest.json'


def main():
    manifest = json.loads(MANIFEST.read_text(encoding='utf-8'))
    expected = manifest['files']
    errors = []
    total = 0
    candidate = Path('C:/Program Files/Git/bin/bash.exe')
    # Prefer native Git Bash over the legacy WSL launcher on Windows.
    bash = str(candidate) if sys.platform == 'win32' and candidate.is_file() else shutil.which('bash')
    shell_count = 0
    for name, metadata in sorted(expected.items()):
        rel = PurePosixPath(name)
        if rel.is_absolute() or '..' in rel.parts or '\\' in name or ':' in name:
            errors.append('Invalid manifest path: ' + name)
            continue
        path = ROOT / name
        if not path.is_file() or path.is_symlink():
            errors.append('Missing or linked source: ' + name)
            continue
        data = path.read_bytes()
        total += len(data)
        if len(data) != metadata['bytes'] or hashlib.sha256(data).hexdigest() != metadata['sha256']:
            errors.append('Source differs from reviewed snapshot: ' + name)
        if len(data) > 1024 * 1024 or b'\x00' in data:
            errors.append('Unexpected binary or large source: ' + name)
            continue
        try:
            content = data.decode('utf-8')
            if path.suffix == '.py':
                ast.parse(content, filename=name)
            if path.suffix == '.sh' and bash:
                result = subprocess.run([bash, '-n', str(path)], capture_output=True, text=True)
                shell_count += 1
                if result.returncode:
                    errors.append(name + ': ' + result.stderr.strip())
        except (UnicodeError, SyntaxError) as error:
            errors.append(name + ': ' + str(error))
    # Check what Git could publish, not ignored local build products.
    if (ROOT / '.git').is_dir():
        result = subprocess.run(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'],
                                cwd=ROOT, capture_output=True, check=True)
        visible = set(result.stdout.decode('utf-8').rstrip('\x00').split('\x00')) - {''}
        unexpected = visible - set(expected) - {'source-manifest.json'}
        errors.extend('File not in reviewed allowlist: ' + name for name in sorted(unexpected))
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        return 1
    print(f'OK: {len(expected)} manifest-listed files, {total:,} bytes; Python syntax checked.')
    print(f'Shell syntax checked: {shell_count} files.' if bash else 'Shell syntax not checked: Bash unavailable.')
    print('No phone access or runtime execution performed.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
