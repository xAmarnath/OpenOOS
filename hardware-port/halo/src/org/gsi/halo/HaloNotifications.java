package org.gsi.halo;
import android.service.notification.*;
import java.util.concurrent.*;
public final class HaloNotifications extends NotificationListenerService {
 private final ExecutorService io=Executors.newSingleThreadExecutor();private long last;
 @Override public void onNotificationPosted(StatusBarNotification n){
  if(n==null||n.isOngoing()||n.getPackageName().equals(getPackageName()))return;
  long now=android.os.SystemClock.elapsedRealtime();if(now-last<10000)return;last=now;
  io.execute(()->{try{HaloClient.request("pulse 1730303 20 2000");}catch(Exception ignored){}});
 }
 @Override public void onDestroy(){io.shutdown();super.onDestroy();}
}
