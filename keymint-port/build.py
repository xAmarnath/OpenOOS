from pathlib import Path
import subprocess,os,sys
p=Path('network-port').resolve();gb=p/'libgbinder';gu=p/'libglibutil';a=p/'alpine/usr'
base=[str(Path('toolchain/zig-windows-x86_64-0.13.0/zig.exe').resolve()),'cc','-target','aarch64-linux-musl','-static','-O2','-fno-sanitize=all','-D_GNU_SOURCE','-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_32','-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_84']
for d in [Path('.').resolve(),gb/'include',gb/'src',gu/'include',a/'include/glib-2.0',a/'lib/glib-2.0/include',a/'include']:base+=['-I',str(d)]
sources=[str(x) for d in [gb/'src',gu/'src'] for x in d.glob('*.c')]
links=['-L',str(a/'lib'),'-lgobject-2.0','-lglib-2.0','-lffi','-lintl','-lpcre2-8','-lm','-lpthread']
os.environ['ZIG_GLOBAL_CACHE_DIR']=str(Path('toolchain/zig-cache').resolve())
name=sys.argv[1] if len(sys.argv)>1 else 'hardware-probe'
if name=='guest-gatekeeper':
    base[1]='c++'
    cmd=base+['-I',str(Path('keymint-port/gatekeeper/core/include').resolve()),'-x','c']+sources+['-x','c++']+[str(Path('keymint-port')/n) for n in ('guest-gatekeeper.cpp','gatekeeper/core/gatekeeper.cpp','gatekeeper/core/gatekeeper_messages.cpp')]+links+['-lcrypto','-o',str(Path('keymint-port')/name)]
else:
    cmd=base+sources+[str(Path('keymint-port')/(name+'.c'))]+links+['-o',str(Path('keymint-port')/name)]
r=subprocess.run(cmd,capture_output=True,text=True)
Path('keymint-port',name+'-build.log').write_text(r.stdout+r.stderr,encoding='utf8')
print(r.returncode,(r.stdout+r.stderr)[-4000:]);sys.exit(r.returncode)
