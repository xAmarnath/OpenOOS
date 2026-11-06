"""Copy only stock Google APKs and matching permission declarations into staging."""
from pathlib import Path
import shlex
import sys
import xml.etree.ElementTree as ET
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from device import root, push, S

packages = {'com.google.android.gms': 'GmsCore', 'com.android.vending': 'Phonesky'}
paths = root('find /data/app -maxdepth 3 -name "*.apk"').decode().splitlines()
commands = ['set -eu', f'mkdir -p {S}/gapps/GoogleServicesFramework']
for package, folder in packages.items():
    apks = [p for p in paths if '/'+package+'-' in p]
    if not any(p.endswith('/base.apk') for p in apks):
        raise RuntimeError('Missing APK set: '+package)
    parents = {p.rsplit('/', 1)[0] for p in apks}
    if len(parents) != 1:
        raise RuntimeError('Ambiguous installed APK versions: '+package)
    commands.append(f'mkdir -p {S}/gapps/{folder}')
    for apk in apks:
        commands.append(f'cp {shlex.quote(apk)} {S}/gapps/{folder}/{apk.rsplit("/",1)[1]}')
commands.append(f'cp /system_ext/priv-app/GoogleServicesFramework/GoogleServicesFramework.apk {S}/gapps/GoogleServicesFramework/base.apk')
commands.append(f'chmod -R a+rX {S}/gapps')
commands.append(f'chcon -R u:object_r:system_file:s0 {S}/gapps')
commands.append(f'du -sh {S}/gapps/*')
print(root('\n'.join(commands), timeout=60).decode(), flush=True)
result = ET.Element('permissions')
wanted = set(packages) | {'com.google.android.gsf'}
for part in ['system', 'system_ext', 'product']:
    name=f'privapp-permissions-google-{part}.xml'
    data=root(f'cat /{part}/etc/permissions/{name}')
    Path(__file__).with_name(name).write_bytes(data)
    for item in ET.fromstring(data):
        if item.tag=='privapp-permissions' and item.get('package') in wanted:
            result.append(item)
extra = {
    'com.android.vending': ['MANAGE_DEFAULT_SEARCH_ENGINE', 'GET_ROLE_HOLDERS'],
    'com.google.android.gms': ['ACCESS_COMPANION_INFO', 'INSTALL_LOCATION_PROVIDER'],
}
for item in result:
    present = {p.get('name') for p in item}
    for permission in extra.get(item.get('package'), []):
        name = 'android.permission.' + permission
        if name not in present:
            ET.SubElement(item, 'permission', name=name)
target=Path(__file__).with_name('guest-gapps-permissions.xml')
ET.indent(result)
ET.ElementTree(result).write(target, encoding='utf-8', xml_declaration=True)
push(target, S+'/guest-gapps-permissions.xml')
print('Staged APKs and permissions; no account data copied.')
