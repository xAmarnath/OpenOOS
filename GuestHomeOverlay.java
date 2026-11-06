import java.lang.reflect.*;
public class GuestHomeOverlay {
    public static void main(String[] args) { try { go(); } catch(Throwable e) { e.printStackTrace(System.out); } }
    public static void go() throws Exception {
        Class<?> fb = Class.forName("android.content.om.FabricatedOverlay$Builder");
        Object builder = fb.getConstructor(String.class,String.class,String.class)
            .newInstance("com.android.shell","GuestCleanHome","com.android.launcher3");
        for (Method m : fb.getMethods()) {
            if (m.getName().equals("setResourceValue")) System.out.println(m);
        }
        Method dim = fb.getMethod("setResourceValue",String.class,int.class,int.class,String.class);
        for (String name : new String[]{"qsb_widget_height","qsb_shadow_height"})
            dim.invoke(builder,"com.android.launcher3:dimen/"+name,16,0,null);
        Object overlay = fb.getMethod("build").invoke(builder);
        for (Field f : overlay.getClass().getDeclaredFields()) {
            f.setAccessible(true); Object v=f.get(overlay);
            if(v!=null && v.getClass().getName().endsWith("FabricatedOverlayInternal")) {
                Field es=v.getClass().getField("entries");
                for(Object e:(java.util.List<?>)es.get(v)) e.getClass().getField("dataType").setInt(e,5);
            }
        }
        Class<?> txb = Class.forName("android.content.om.OverlayManagerTransaction$Builder");
        Object transactionBuilder = txb.getConstructor().newInstance();
        txb.getMethod("registerFabricatedOverlay",overlay.getClass()).invoke(transactionBuilder,overlay);
        Object id = overlay.getClass().getMethod("getIdentifier").invoke(overlay);
        txb.getMethod("setEnabled",id.getClass(),boolean.class,int.class).invoke(transactionBuilder,id,true,0);
        Object transaction = txb.getMethod("build").invoke(transactionBuilder);
        Object binder = Class.forName("android.os.ServiceManager").getMethod("getService",String.class).invoke(null,"overlay");
        Object manager = Class.forName("android.content.om.IOverlayManager$Stub")
            .getMethod("asInterface",Class.forName("android.os.IBinder")).invoke(null,binder);
        Class.forName("android.content.om.IOverlayManager").getMethod("commit",transaction.getClass()).invoke(manager,transaction);
        System.out.println("Guest clean home overlay enabled");
    }
}
