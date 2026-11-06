import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.*;
import android.view.Display;
import java.io.*;
import java.lang.reflect.*;
public final class GuestBrightness {
 public static void main(String[] args)throws Exception {
  Looper.prepare();Class<?> at=Class.forName("android.app.ActivityThread");Object thread=at.getMethod("systemMain").invoke(null);
  Context context=(Context)at.getMethod("getSystemContext").invoke(thread);
  DisplayManager dm=(DisplayManager)context.getSystemService(Context.DISPLAY_SERVICE);
  Method get=DisplayManager.class.getMethod("getBrightness",int.class);
  File node=new File("/data/local/tmp/panel-brightness");
  if(!node.exists())throw new IOException("Panel control not mounted");
  int last=-1;
  while(true){
   Display display=dm.getDisplay(Display.DEFAULT_DISPLAY);
   if(display!=null&&display.getState()==Display.STATE_ON){
    float value=(Float)get.invoke(dm,Display.DEFAULT_DISPLAY);
    if(Float.isFinite(value)&&value>=0&&value<=1){
     // I2401 panel 0x30: SensorFrameworkConfig MaxBrightness=7032.
     int target=Math.max(2,Math.min(7032,(int)Math.ceil(value*7032)));
     if(target!=last){try(FileOutputStream out=new FileOutputStream(node)){out.write((target+"\n").getBytes("US-ASCII"));}last=target;System.out.println("Brightness "+value+" -> "+target);}
    }
   }
   Thread.sleep(250);
  }
 }
}
