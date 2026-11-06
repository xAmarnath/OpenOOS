package org.gsi.halo;
import android.app.*;
import android.os.*;
import android.content.*;
import android.view.*;
import android.widget.*;
import android.provider.Settings;
import java.util.concurrent.*;
public final class MainActivity extends Activity {
 final ExecutorService io=Executors.newSingleThreadExecutor(); final Handler handler=new Handler(Looper.getMainLooper()); TextView status;Spinner colors;SeekBar level; Button preview,off; Switch charging; boolean syncing;
 final int[] values={1730303,11176038,16731392,16747520,16715008,1769216,3407693,10027263,16718362};
 int dp(int x){return Math.round(x*getResources().getDisplayMetrics().density);}
 TextView text(LinearLayout box,String value,int size){TextView t=new TextView(this);android.util.TypedValue color=new android.util.TypedValue();getTheme().resolveAttribute(android.R.attr.textColorPrimary,color,true);t.setTextColor(getColorStateList(color.resourceId));t.setText(value);t.setTextSize(size);t.setPadding(0,dp(8),0,dp(8));box.addView(t);return t;}
 Button button(LinearLayout box,String label,Runnable action){Button b=new Button(this);b.setText(label);b.setMinHeight(dp(48));LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.setMargins(0,dp(4),0,dp(4));box.addView(b,p);b.setOnClickListener(v->action.run());return b;}
 @Override public void onCreate(Bundle b){super.onCreate(b);
  ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);LinearLayout box=new LinearLayout(this);box.setOrientation(1);box.setPadding(dp(24),dp(16),dp(24),dp(24));scroll.addView(box);setContentView(scroll);
  scroll.setOnApplyWindowInsetsListener((v,i)->{android.graphics.Insets n=i.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());v.setPadding(n.left,n.top,n.right,n.bottom);return i;});
  TextView title=text(box,"Monster Halo",30);title.setAccessibilityHeading(true);text(box,"Rear lighting · iQOO 13",16);
  status=text(box,"Connecting…",16);status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
  text(box,"Color",18);colors=new Spinner(this);colors.setContentDescription("Light color");colors.setMinimumHeight(dp(48));colors.setAdapter(new ArrayAdapter<String>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"Cyan","White","Orange","Yellow","Red","Green","Mint","Purple","Pink"}));box.addView(colors,new LinearLayout.LayoutParams(-1,dp(56)));
  TextView brightness=text(box,"Light intensity: 20%",18);level=new SeekBar(this);level.setMin(1);level.setMax(100);level.setProgress(20);level.setContentDescription("Light intensity");box.addView(level,new LinearLayout.LayoutParams(-1,dp(56)));level.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onStartTrackingTouch(SeekBar s){} public void onStopTrackingTouch(SeekBar s){}public void onProgressChanged(SeekBar s,int p,boolean u){brightness.setText("Light intensity: "+p+"%");}});
  preview=button(box,"Preview for 3 seconds",()->command("pulse "+values[colors.getSelectedItemPosition()]+" "+level.getProgress()+" 3000"));
  off=button(box,"Turn off",()->command("stop"));
  text(box,"Automatic lighting",22).setAccessibilityHeading(true);
  charging=new Switch(this);charging.setText("Pulse when charging starts");charging.setMinHeight(dp(56));box.addView(charging);charging.setOnCheckedChangeListener((v,on)->{if(!syncing)command("charging "+(on?1:0));});
  button(box,"Notification lighting",()->startActivity(new Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)));
  text(box,"Enable Monster Halo in notification access to show a short light when a new notification arrives. It does not read message text.",14);
  text(box,"Previews stop automatically after three seconds. Use Turn off to end a preview early.",14);
 }
 void command(String cmd){preview.setEnabled(false);charging.setEnabled(false);status.setText("Sending…");io.execute(()->{String response;Boolean enabled=null;try{response=HaloClient.request(cmd);String state=HaloClient.request("charging?");if(state.equals("0")||state.equals("1"))enabled=state.equals("1");}catch(Exception e){response="Unavailable — start the guest hardware service.";}final String result=response;final Boolean checked=enabled;runOnUiThread(()->{if(isDestroyed())return;status.setText(result);preview.setEnabled(true);if(checked!=null){syncing=true;charging.setChecked(checked);syncing=false;charging.setEnabled(true);}if(cmd.startsWith("pulse "))handler.postDelayed(()->{if(!isDestroyed())command("status");},3300);});});}
 @Override protected void onResume(){super.onResume();command("status");}
 @Override protected void onDestroy(){handler.removeCallbacksAndMessages(null);io.shutdown();super.onDestroy();}
}
