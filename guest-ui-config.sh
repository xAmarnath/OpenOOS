#!/system/bin/sh
set -eu
# Execute only inside the checked, isolated running guest.
settings put global window_animation_scale 1.0
settings put global transition_animation_scale 1.0
settings put global animator_duration_scale 1.0
settings put system screen_off_timeout 1200000
settings put system font_scale 1.0
settings put system pointer_location 0
settings put system show_touches 0
settings put system sound_effects_enabled 0
settings put system haptic_feedback_enabled 0
# Imported Play Store runs a systemExempted download foreground service.
# Keep this exemption scoped to its package in guest AppOps storage.
if pm path com.android.vending | grep -q '^package:'; then
    cmd appops set com.android.vending SYSTEM_EXEMPT_FROM_POWER_RESTRICTIONS allow
fi
pm enable --user 0 com.android.bluetooth
# The imported Vivo Camera requires proprietary framework classes unavailable
# in this guest. Keep its broken launcher entry hidden after a data reset.
if pm path com.android.camera | grep -q '^package:'; then
    pm disable-user --user 0 com.android.camera
fi
# Hide legacy launcher entries only when their replacement is installed.
# Keep the original telephony services/providers available for system use.
if pm path org.fossify.calendar | grep -q '^package:'; then
    pm disable --user 0 com.android.calendar/.AllInOneActivity
fi
if pm path org.fossify.contacts | grep -q '^package:'; then
    pm disable --user 0 com.android.contacts/.activities.PeopleActivity
fi
if cmd role get-role-holders --user 0 android.app.role.SMS | grep -qx org.fossify.messages; then
    pm disable --user 0 com.android.messaging/.ui.conversationlist.ConversationListActivity
fi
if cmd role get-role-holders --user 0 android.app.role.DIALER | grep -qx org.fossify.phone; then
    pm disable --user 0 com.android.dialer/.main.impl.MainActivity
fi
if pm path net.sourceforge.opencamera | grep -q '^package:'; then
    pm disable --user 0 com.android.camera2/com.android.camera.CameraLauncher
fi
if pm path org.chromium.webview_shell | grep -q '^package:'; then
    pm disable-user --user 0 org.chromium.webview_shell
fi
/system/bin/sh /data/local/tmp/guest-home-overlay.sh
# SystemUI can keep its old view padding after a fabricated overlay update.
# Refresh that layout, then restore the 1.0 font scale selected above.
settings put system font_scale 1.001
sleep 2
settings put system font_scale 1.0
# Match stock's working native 120 Hz baseline.
# These SettingsProvider values belong only to the guest userdata image.
settings put system min_refresh_rate 120.0
settings put system peak_refresh_rate 120.0
settings put global device_name "iQOO 13"
cmd overlay enable-exclusive --user 0 --category com.android.internal.systemui.navbar.gestural
input keyevent KEYCODE_WAKEUP
wm dismiss-keyguard
am start -a android.intent.action.MAIN -c android.intent.category.HOME
am start -a android.settings.SETTINGS
wm size
wm density
settings get secure navigation_mode
settings get global window_animation_scale
settings get global transition_animation_scale
settings get global animator_duration_scale
echo "OpenOOS UI configuration complete"
