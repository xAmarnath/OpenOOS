import android.content.*;
import android.net.*;
import android.os.*;
import java.io.*;
import java.lang.reflect.*;
import java.util.*;
public final class HaloDaemon {
 static Class<?> api,stateClass;static Object hal;static Handler h;static long generation,until;static boolean charging;static int plugged=-1;
 static final Set<Integer> COLORS=new HashSet<>(Arrays.asList(1730303,11176038,16731392,16747520,16715008,1769216,3407693,10027263,16718362));
 static final File config=new File("/data/local/tmp/halo-charging");
 static void stop()throws Exception{api.getMethod("stopLight",int.class).invoke(hal,6);generation++;until=0;System.out.println("HAL stopped");}
 static String pulse(int color,int brightness,int duration)throws Exception {
  if(!COLORS.contains(color)||brightness<1||brightness>100||duration<100||duration>5000)throw new IllegalArgumentException("Out of range");
  Object s=stateClass.getConstructor().newInstance();stateClass.getField("mode").setByte(s,(byte)2);stateClass.getField("color").setInt(s,color);stateClass.getField("brightness").setInt(s,brightness);stateClass.getField("duration_ms").setInt(s,duration);
  int result=(Integer)api.getMethod("setLightEffect",int.class,stateClass).invoke(hal,6,s);if(result!=0)throw new IOException("HAL result "+result);
  long token=++generation;until=SystemClock.elapsedRealtime()+duration;h.postDelayed(()->{if(generation==token)try{stop();}catch(Exception e){System.out.println(e);}},duration+100);
  System.out.println("HAL accepted color="+color+" brightness="+brightness+" duration="+duration);return "Preview accepted · "+(duration/1000.0)+" seconds";
 }
 static String command(String line)throws Exception {
  String[] a=line.trim().split(" +");
  if(a.length==1&&a[0].equals("status"))return "Connected · "+(until>SystemClock.elapsedRealtime()?"preview active":"no active preview");
  if(a.length==1&&a[0].equals("charging?"))return charging?"1":"0";
  if(a.length==1&&a[0].equals("stop")){stop();return "Stop command sent";}
  if(a.length==4&&a[0].equals("pulse"))return pulse(Integer.parseInt(a[1]),Integer.parseInt(a[2]),Integer.parseInt(a[3]));
  if(a.length==2&&a[0].equals("charging")&&(a[1].equals("0")||a[1].equals("1"))){boolean enabled=a[1].equals("1");try(FileOutputStream f=new FileOutputStream(config)){f.write(enabled?49:48);}charging=enabled;return charging?"Charging pulse enabled":"Charging pulse off";}
  throw new IllegalArgumentException("Unknown command");
 }
 public static void main(String[] args)throws Exception {
  int allowedUid=Integer.parseInt(args[0]);if(allowedUid<10000||allowedUid>19999)throw new IllegalArgumentException("App UID");
  Looper.prepare();h=new Handler(Looper.myLooper());
  Object b=Class.forName("android.os.ServiceManager").getMethod("checkService",String.class).invoke(null,"vendor.vivo.hardware.vlight.IVlight/default");
  if(b==null)throw new IOException("HAL unavailable");api=Class.forName("vendor.vivo.hardware.vlight.IVlight");stateClass=Class.forName("vendor.vivo.hardware.vlight.VlightState");hal=Class.forName("vendor.vivo.hardware.vlight.IVlight$Stub").getMethod("asInterface",IBinder.class).invoke(null,b);
  System.out.println("Halo ready: HAL version "+api.getMethod("getInterfaceVersion").invoke(hal)+" hardware "+api.getMethod("hasLight",int.class).invoke(hal,6));
  try(FileInputStream f=new FileInputStream(config)){charging=f.read()==49;}catch(IOException ignored){}
  Class<?> at=Class.forName("android.app.ActivityThread");Object t=at.getMethod("systemMain").invoke(null);Context c=(Context)at.getMethod("getSystemContext").invoke(t);
  c.registerReceiver(new BroadcastReceiver(){public void onReceive(Context c,Intent i){int p=i.getIntExtra(BatteryManager.EXTRA_PLUGGED,0);if(plugged==0&&p!=0&&charging)try{pulse(1769216,20,3000);}catch(Exception e){System.out.println(e);}plugged=p;}},new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
  LocalServerSocket server=new LocalServerSocket("gsi17.monsterhalo");
  new Thread(()->{while(true)try{LocalSocket s=server.accept();s.setSoTimeout(1000);int uid=s.getPeerCredentials().getUid();if(uid!=allowedUid&&uid!=0){s.close();continue;}
   ByteArrayOutputStream bytes=new ByteArrayOutputStream();int ch;while(bytes.size()<128&&(ch=s.getInputStream().read())!=-1&&ch!=10)bytes.write(ch);
   if(bytes.size()>=128){s.close();continue;}String line=bytes.toString("UTF-8");
   h.post(()->{try(LocalSocket client=s){String result;try{result=command(line);}catch(Exception e){result="Error: "+e.getClass().getSimpleName();System.out.println(e);}client.getOutputStream().write((result+"\n").getBytes("UTF-8"));}catch(IOException e){System.out.println(e);}});
  }catch(IOException e){System.out.println(e);}},"HaloSocket").start();
  Runtime.getRuntime().addShutdownHook(new Thread(()->{try{stop();}catch(Exception ignored){}}));Looper.loop();
 }
}
