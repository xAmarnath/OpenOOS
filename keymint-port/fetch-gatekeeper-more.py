import base64,io,tarfile
from pathlib import Path
from urllib.request import urlopen
from concurrent.futures import ThreadPoolExecutor
p=Path('keymint-port/gatekeeper')
refs={
 'soft_keymaster_enforcement.h':('system/keymaster','include/keymaster/km_openssl/soft_keymaster_enforcement.h'),
 'ckdf.cpp':('system/keymaster','km_openssl/ckdf.cpp'),
 **{'core/include/gatekeeper/'+n:('system/gatekeeper','include/gatekeeper/'+n) for n in ('UniquePtr.h','gatekeeper_utils.h')},
 'core/gatekeeper_utils.cpp':('system/gatekeeper','gatekeeper_utils.cpp'),
 'core/include/hardware/hw_auth_token.h':('hardware/libhardware','include_all/hardware/hw_auth_token.h'),
}
def fetch(item):
 n,(repo,path)=item
 try:
  data=base64.b64decode(urlopen(f'https://android.googlesource.com/platform/{repo}/+/refs/heads/main/{path}?format=TEXT',timeout=25).read())
  dest=p/n;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
  return n,len(data)
 except Exception as e:return n,str(e)
with ThreadPoolExecutor(max_workers=4) as pool:
 for r in pool.map(fetch,refs.items()):print(r,flush=True)
index={}
for chunk in Path('network-port/APKINDEX').read_text(encoding='utf8').split('\n\n'):
 d=dict(l.split(':',1) for l in chunk.splitlines() if ':' in l)
 if 'P' in d:index[d['P']]=d['V']
for pkg in ['openssl-dev','openssl-libs-static']:
 data=urlopen(f'https://dl-cdn.alpinelinux.org/alpine/v3.22/main/aarch64/{pkg}-{index[pkg]}.apk',timeout=30).read()
 with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as t:
  for m in t:
   if not m.isfile():continue
   parts=Path(m.name).parts
   if not parts or '..' in parts or Path(m.name).is_absolute():continue
   q=Path('network-port/alpine').joinpath(*parts);q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(t.extractfile(m).read())
 print(pkg,len(data),flush=True)
