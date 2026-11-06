from pathlib import Path
import subprocess,sys
import os
sdk=Path(os.environ['ANDROID_SDK_ROOT'])
src=Path(sys.argv[1]); output=Path(sys.argv[2])
classes=output.parent/(output.stem+'-classes'); classes.mkdir(exist_ok=True)
subprocess.run(['javac','-source','8','-target','8','-cp',str(sdk/'platforms/android-37.0/android.jar'),'-d',str(classes),str(src)],check=True)
subprocess.run([str(sdk/'build-tools/37.0.0/d8.bat'),'--lib',str(sdk/'platforms/android-37.0/android.jar'),'--output',str(output),*[str(p) for p in classes.rglob('*.class')]],check=True)
