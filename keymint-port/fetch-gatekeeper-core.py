import base64
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from urllib.request import urlopen

p = Path('keymint-port/gatekeeper')
refs = {
 'SharedSecret.cpp':('hardware/interfaces','gatekeeper/aidl/software/SharedSecret.cpp'),
 'SharedSecret.h':('hardware/interfaces','gatekeeper/aidl/software/SharedSecret.h'),
 'core/gatekeeper.cpp':('system/gatekeeper','gatekeeper.cpp'),
 'core/gatekeeper_messages.cpp':('system/gatekeeper','gatekeeper_messages.cpp'),
 **{'core/include/gatekeeper/'+n:('system/gatekeeper','include/gatekeeper/'+n) for n in ('gatekeeper.h','gatekeeper_messages.h','password_handle.h')},
 'soft_keymaster_enforcement.cpp':('system/keymaster','contexts/soft_keymaster_enforcement.cpp'),
 'soft_keymaster_context.cpp':('system/keymaster','contexts/soft_keymaster_context.cpp'),
 'pure_soft_keymaster_context.cpp':('system/keymaster','contexts/pure_soft_keymaster_context.cpp'),
 'soft_keymaster_enforcement.h':('system/keymaster','include/keymaster/soft_keymaster_enforcement.h'),
}
def fetch(item):
    name,(repo,path)=item
    dest=p/name
    if dest.exists():return name,'cached'
    try:
        data=base64.b64decode(urlopen(f'https://android.googlesource.com/platform/{repo}/+/refs/heads/main/{path}?format=TEXT',timeout=25).read())
        dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_bytes(data)
        return name,len(data)
    except Exception as e:return name,str(e)
with ThreadPoolExecutor(max_workers=4) as pool:
    for result in pool.map(fetch,refs.items()):print(result,flush=True)
