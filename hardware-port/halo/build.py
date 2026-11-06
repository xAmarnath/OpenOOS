from pathlib import Path
import subprocess,zipfile
here=Path(__file__).resolve().parent
import os
sdk=Path(os.environ['ANDROID_SDK_ROOT']); bt=sdk/'build-tools/37.0.0'; android=sdk/'platforms/android-37.0/android.jar'
out=here/'build';out.mkdir(exist_ok=True);classes=out/'classes';classes.mkdir(exist_ok=True)
def run(*args):subprocess.run([str(x) for x in args],check=True)
run('javac','-source','8','-target','8','-cp',android,'-d',classes,*here.glob('src/org/gsi/halo/*.java'))
run(bt/'aapt2.exe','compile','--dir',here/'res','-o',out/'resources.zip')
run(bt/'aapt2.exe','link','-I',android,'--manifest',here/'AndroidManifest.xml','-o',out/'unsigned.apk',out/'resources.zip')
run(bt/'d8.bat','--lib',android,'--output',out,*classes.rglob('*.class'))
with zipfile.ZipFile(out/'unsigned.apk','a') as z:z.write(out/'classes.dex','classes.dex')
run(bt/'zipalign.exe','-f','4',out/'unsigned.apk',out/'aligned.apk')
key=here/'guest-dev.jks'
if not key.exists():run('keytool','-genkeypair','-keystore',key,'-storepass','guest-development','-keypass','guest-development','-alias','guest','-keyalg','RSA','-keysize','2048','-validity','3650','-dname','CN=Guest Development')
run(bt/'apksigner.bat','sign','--ks',key,'--ks-pass','pass:guest-development','--out',here/'MonsterHalo.apk',out/'aligned.apk')
run(bt/'apksigner.bat','verify',here/'MonsterHalo.apk')
