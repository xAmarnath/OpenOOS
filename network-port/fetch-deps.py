from pathlib import Path
import urllib.request,tarfile,io,concurrent.futures
p=Path('network-port');sysroot=p/'alpine';sysroot.mkdir(exist_ok=True)
index={}
for chunk in (p/'APKINDEX').read_text(encoding='utf8').split('\n\n'):
 d=dict(l.split(':',1) for l in chunk.splitlines() if ':' in l)
 if 'P' in d:index[d['P']]=d['V']
jobs=[]
for pkg in ['glib-dev','glib-static','pcre2-static','libffi-dev','gettext-static']:
 jobs.append((f'https://dl-cdn.alpinelinux.org/alpine/v3.22/main/aarch64/{pkg}-{index[pkg]}.apk',sysroot,False))
for repo in ['mer-hybris/libgbinder','sailfishos/libglibutil']:
 jobs.append((f'https://codeload.github.com/{repo}/tar.gz/refs/heads/master',p/repo.split('/')[1],True))
def get(job):
 url,dest,strip=job
 b=urllib.request.urlopen(url,timeout=30).read()
 with tarfile.open(fileobj=io.BytesIO(b),mode='r:gz') as t:
  for m in t:
   if not m.isfile():continue
   parts=Path(m.name).parts[1:] if strip else Path(m.name).parts
   if not parts or '..' in parts:continue
   q=dest.joinpath(*parts);q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(t.extractfile(m).read())
 return url,len(b)
with concurrent.futures.ThreadPoolExecutor(max_workers=5) as e:
 for res in e.map(get,jobs):print(res)
