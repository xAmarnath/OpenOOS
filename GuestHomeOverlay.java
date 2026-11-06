import java.lang.reflect.*;
public class GuestHomeOverlay {
    public static void main(String[] args) {
        try {
            if (args.length == 1 && args[0].equals("--display-insets-only")) displayInsets();
            else go();
        } catch(Throwable e) { e.printStackTrace(System.out); System.exit(1); }
    }
    public static void go() throws Exception {
        register("GuestCleanHome", "com.android.launcher3",
            new String[]{"qsb_widget_height", "qsb_shadow_height"}, new int[]{0, 0});
        displayInsets();
    }
    public static void displayInsets() throws Exception {
        // I2401: 120 px panel corners at 600 dpi. Keep status content inside
        // the rounded edge without changing the display mode or cutout shape.
        // TYPE_DIMENSION complex values: integral dp is (value << 8) | 1.
        register("OpenOOSStatusInsets", "com.android.systemui",
            new String[]{"status_bar_padding_start", "status_bar_padding_end", "status_bar_padding_top"},
            new int[]{(24 << 8) | 1, (24 << 8) | 1, (4 << 8) | 1});
    }
    private static void register(String name, String target, String[] resources, int[] values) throws Exception {
        Class<?> fb = Class.forName("android.content.om.FabricatedOverlay$Builder");
        Object builder = fb.getConstructor(String.class,String.class,String.class)
            .newInstance("com.android.shell",name,target);
        Method dim = fb.getMethod("setResourceValue",String.class,int.class,int.class,String.class);
        for (int i = 0; i < resources.length; i++)
            dim.invoke(builder,target+":dimen/"+resources[i],16,values[i],null);
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
        System.out.println(name+" overlay enabled for "+target);
    }
}
