from pathlib import Path
import subprocess,os
p=Path('network-port').resolve();gb=p/'libgbinder';gu=p/'libglibutil';a=p/'alpine/usr'
# All configuration lives under the user's temporary guest workspace.
f=gb/'src/gbinder_config.c';s=f.read_text();s=s.replace('"/etc/gbinder.conf"','"/data/local/tmp/gsi17-graphics-20261105-01/gbinder.conf"').replace('"/etc/gbinder.d"','"/data/local/tmp/gsi17-graphics-20261105-01/gbinder.d"');f.write_text(s,newline='\n')
base=[str(Path('toolchain/zig-windows-x86_64-0.13.0/zig.exe').resolve()),'cc','-target','aarch64-linux-musl','-static','-O2','-fno-sanitize=all','-D_GNU_SOURCE','-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_32','-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_84']
for d in [gb/'include',gu/'include',a/'include/glib-2.0',a/'lib/glib-2.0/include',a/'include']:base+=['-I',str(d)]
sources=[str(x) for d in [gb/'src',gu/'src'] for x in d.glob('*.c')]
links=['-L',str(a/'lib'),'-lgobject-2.0','-lglib-2.0','-lffi','-lintl','-lpcre2-8','-lm','-lpthread']
os.environ['ZIG_GLOBAL_CACHE_DIR']=str(Path('toolchain/zig-cache').resolve())
base += ['-I',str(Path('.').resolve()),'-s']
for tool in ['hardware-bridge']:
 cmd=base+sources+[str(p/f'{tool}.c')]+links+['-o',str(p/tool)]
 print('Building',tool,flush=True)
 r=subprocess.run(cmd,capture_output=True,text=True);(p/(tool+'-build.log')).write_text(r.stdout+r.stderr,encoding='utf8');print(r.returncode,(r.stdout+r.stderr)[-9000:],flush=True)
 if r.returncode:break
