"""Build with Android command-line tools; keep generated files outside public source."""
from pathlib import Path
import argparse, os, subprocess, zipfile
p = argparse.ArgumentParser()
p.add_argument('--sdk', type=Path, default=os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT'))
p.add_argument('--out', type=Path, required=True)
a = p.parse_args(); here = Path(__file__).resolve().parent; out = a.out.resolve(); out.mkdir(parents=True, exist_ok=True)
if a.sdk is None:
    p.error('Set ANDROID_HOME / ANDROID_SDK_ROOT or pass --sdk.')
bt = a.sdk / 'build-tools/37.0.0'; android = a.sdk / 'platforms/android-37.0/android.jar'
classes = out / 'classes'; classes.mkdir(exist_ok=True)
def run(*cmd): subprocess.run([str(x) for x in cmd], check=True)
run('javac', '-source', '8', '-target', '8', '-cp', android, '-d', classes, *here.glob('src/org/openoos/switcher/*.java'))
run(bt/'aapt2.exe', 'compile', '--dir', here/'res', '-o', out/'resources.zip')
run(bt/'aapt2.exe', 'link', '-I', android, '--manifest', here/'AndroidManifest.xml', '-o', out/'unsigned.apk', out/'resources.zip')
run(bt/'d8.bat', '--lib', android, '--min-api', '30', '--output', out, *classes.rglob('*.class'))
with zipfile.ZipFile(out/'unsigned.apk', 'a') as z: z.write(out/'classes.dex', 'classes.dex')
run(bt/'zipalign.exe', '-f', '4', out/'unsigned.apk', out/'aligned.apk')
key = out/'switcher-local.jks'
if not key.exists():
    run('keytool', '-genkeypair', '-keystore', key, '-storepass', 'openoos-local', '-keypass', 'openoos-local', '-alias', 'switcher', '-keyalg', 'RSA', '-keysize', '3072', '-validity', '3650', '-dname', 'CN=OpenOOS Local Switcher')
run(bt/'apksigner.bat', 'sign', '--ks', key, '--ks-pass', 'pass:openoos-local', '--out', out/'OpenOOS-Switch.apk', out/'aligned.apk')
run(bt/'apksigner.bat', 'verify', out/'OpenOOS-Switch.apk')
print(out/'OpenOOS-Switch.apk')
