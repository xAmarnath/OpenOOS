#!/system/bin/sh
set -eu
export ANDROID_DATA=/data ANDROID_ROOT=/system
export ANDROID_ART_ROOT=/apex/com.android.art ANDROID_RUNTIME_ROOT=/apex/com.android.runtime
export ANDROID_I18N_ROOT=/apex/com.android.i18n ANDROID_TZDATA_ROOT=/apex/com.android.tzdata
while read -r action key value; do export "$key=$value"; done < /data/system/environ/classpath
export CLASSPATH=/data/local/tmp/guest-home-overlay.jar
/data/local/tmp/guest-app-process /system/bin GuestHomeOverlay
export CLASSPATH=/data/local/tmp/guest-input-fix.jar
/data/local/tmp/guest-app-process /system/bin GuestInputFix
