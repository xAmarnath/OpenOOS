package org.gsi.halo;
import android.net.*;
import java.io.*;
public final class HaloClient {
 public static String request(String cmd) throws IOException {
  try(LocalSocket s=new LocalSocket()) {
   s.connect(new LocalSocketAddress("gsi17.monsterhalo",LocalSocketAddress.Namespace.ABSTRACT));s.setSoTimeout(2500);
   s.getOutputStream().write((cmd+"\n").getBytes("UTF-8"));s.getOutputStream().flush();
   return new BufferedReader(new InputStreamReader(s.getInputStream(),"UTF-8")).readLine();
  }
 }
}
