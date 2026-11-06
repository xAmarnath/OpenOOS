package org.openoos.switcher;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.content.res.Configuration;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private TextView environment;
    private Button action;
    private boolean active, requesting, changing;
    private String current = "", target = "";
    private int ink, muted, accent, surface;
    private final Runnable poll = () -> request("STATUS");

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private TextView text(String value, int size, int color) {
        TextView t = new TextView(this);
        t.setText(value); t.setTextSize(size); t.setTextColor(color);
        t.setFontFeatureSettings("kern");
        return t;
    }
    private void add(LinearLayout parent, View child, int top) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, -2);
        p.topMargin = dp(top); parent.addView(child, p);
    }
    private GradientDrawable background(int color, int radius) {
        GradientDrawable d = new GradientDrawable(); d.setColor(color); d.setCornerRadius(dp(radius)); return d;
    }
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        boolean dark = (getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES;
        ink = Color.parseColor(dark ? "#F5F5F5" : "#171717");
        muted = Color.parseColor(dark ? "#BDC5D1" : "#475569");
        surface = Color.parseColor(dark ? "#232323" : "#F1F3F5");
        accent = Color.parseColor(dark ? "#EAC96B" : "#A16207");
        int page = Color.parseColor(dark ? "#121212" : "#FFFFFF");
        getWindow().setDecorFitsSystemWindows(false);
        getWindow().setStatusBarColor(Color.TRANSPARENT);
        getWindow().setNavigationBarColor(Color.TRANSPARENT);
        getWindow().getDecorView().setSystemUiVisibility(dark ? 0 : View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        ScrollView scroll = new ScrollView(this); scroll.setFillViewport(true); scroll.setBackgroundColor(page);
        LinearLayout content = new LinearLayout(this); content.setOrientation(LinearLayout.VERTICAL);
        content.setGravity(android.view.Gravity.CENTER_VERTICAL);
        content.setPadding(dp(28), dp(24), dp(28), dp(24)); scroll.addView(content);
        scroll.setOnApplyWindowInsetsListener((v, insets) -> {
            Insets i = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            v.setPadding(i.left, i.top, i.right, i.bottom); return insets;
        });
        add(content, text("Current OS", 16, muted), 0);
        environment = text("Checking...", 30, ink);
        environment.setTypeface(null, Typeface.BOLD);
        environment.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        add(content, environment, 8);
        action = new Button(this); action.setAllCaps(false); action.setTextSize(17); action.setMinHeight(dp(56));
        action.setPadding(dp(20), dp(14), dp(20), dp(14));
        action.setTextColor(dark ? Color.parseColor("#171717") : Color.WHITE);
        action.setBackground(new RippleDrawable(ColorStateList.valueOf(0x337F7F7F), background(accent, 16), null));
        action.setText("Checking..."); action.setEnabled(false); add(content, action, 28);
        action.setOnClickListener(v -> {
            changing = true; action.setEnabled(false); action.setAlpha(.55f);
            action.setText("Switching..."); request(target);
        });
        setContentView(scroll);
    }
    @Override protected void onResume() { super.onResume(); active = true; request("STATUS"); }
    @Override protected void onPause() { active = false; ui.removeCallbacks(poll); super.onPause(); }
    @Override protected void onDestroy() { io.shutdownNow(); super.onDestroy(); }

    private void request(String command) {
        if (isDestroyed()) return;
        if (requesting) { if (changing && !command.equals("STATUS")) ui.postDelayed(() -> request(command), 150); return; }
        requesting = true; ui.removeCallbacks(poll);
        io.execute(() -> {
            String result;
            try (LocalSocket socket = new LocalSocket()) {
                socket.connect(new LocalSocketAddress("openoos.switch.v1", LocalSocketAddress.Namespace.ABSTRACT));
                socket.setSoTimeout(4000);
                socket.getOutputStream().write((command + "\n").getBytes(StandardCharsets.US_ASCII));
                ByteArrayOutputStream reply = new ByteArrayOutputStream();
                int b; while (reply.size() < 2048 && (b = socket.getInputStream().read()) != -1 && b != '\n') reply.write(b);
                result = reply.toString("UTF-8");
            } catch (Exception e) { result = "UNAVAILABLE"; }
            final String value = result;
            ui.post(() -> { requesting = false; if (isDestroyed()) return; render(value); if (active) ui.postDelayed(poll, 2500); });
        });
    }
    private void render(String reply) {
        String[] fields = reply.split("\\t", 4);
        if (fields.length != 4 || !fields[0].equals("OK")) {
            changing = false; current = "";
            environment.setText("Controller unavailable");
            action.setText("Switch unavailable"); action.setEnabled(false); action.setAlpha(.5f); return;
        }
        current = fields[1]; changing = fields[2].equals("BUSY");
        boolean guest = current.equals("GUEST"), ready = fields[2].equals("READY");
        environment.setText(guest ? "OpenOOS" : "Stock Android");
        target = guest ? "STOCK" : "GUEST";
        action.setText(changing ? "Switching…" : guest ? "Return to stock" : "Switch to guest");
        action.setEnabled(ready); action.setAlpha(ready ? 1f : .5f);
    }
}
