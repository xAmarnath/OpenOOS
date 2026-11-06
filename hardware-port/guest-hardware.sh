#!/system/bin/sh
set -eu
until test "$(getprop sys.boot_completed)" = 1; do sleep 1; done
export ANDROID_DATA=/data ANDROID_ROOT=/system ANDROID_ART_ROOT=/apex/com.android.art ANDROID_RUNTIME_ROOT=/apex/com.android.runtime ANDROID_I18N_ROOT=/apex/com.android.i18n ANDROID_TZDATA_ROOT=/apex/com.android.tzdata
while read -r directive key value; do
    case "$key" in BOOTCLASSPATH|DEX2OATBOOTCLASSPATH|SYSTEMSERVERCLASSPATH|STANDALONE_SYSTEMSERVER_JARS) export "$key=$value";; esac
done < /data/system/environ/classpath
export CLASSPATH=/data/local/tmp/guest-brightness.jar
exec /data/local/tmp/guest-app-process /system/bin GuestBrightness
