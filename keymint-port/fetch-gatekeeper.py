import base64
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from urllib.request import urlopen

p = Path('keymint-port/gatekeeper')
p.mkdir(exist_ok=True)
base = 'https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/gatekeeper/aidl/'
files = {n: 'software/' + n for n in ('SoftGateKeeper.cpp', 'SoftGateKeeper.h', 'GateKeeper.cpp', 'GateKeeper.h', 'Android.bp')}
files.update({n: 'android/hardware/gatekeeper/' + n for n in ('IGatekeeper.aidl', 'GatekeeperEnrollResponse.aidl', 'GatekeeperVerifyResponse.aidl')})
def fetch(item):
    name, url = item
    try:
        data = base64.b64decode(urlopen(base + url + '?format=TEXT', timeout=25).read())
        (p / name).write_bytes(data)
        return name, len(data)
    except Exception as e:
        return name, str(e)
with ThreadPoolExecutor(max_workers=4) as pool:
    for result in pool.map(fetch, files.items()):
        print(result, flush=True)
