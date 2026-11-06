from pathlib import Path
import subprocess, os
os.environ['ZIG_GLOBAL_CACHE_DIR']=str(Path('toolchain/zig-cache').resolve())
subprocess.run([str(Path('toolchain/zig-windows-x86_64-0.13.0/zig.exe').resolve()),
    'cc','-target','aarch64-linux-android','-shared','-nostdlib','-fPIC','-O2',
    '-Wl,--allow-shlib-undefined','keymint-port/compat-helper.c',
    '-o','keymint-port/libguest-compat.so'],check=True)
