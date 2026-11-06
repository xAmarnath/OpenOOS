import java.lang.reflect.*;
public class GuestInputFix {
    public static void main(String[] args) throws Exception {
        Object binder=Class.forName("android.os.ServiceManager").getMethod("getService",String.class).invoke(null,"input");
        Class<?> iface=Class.forName("android.hardware.input.IInputManager");
        Object manager=Class.forName("android.hardware.input.IInputManager$Stub").getMethod("asInterface",Class.forName("android.os.IBinder")).invoke(null,binder);
        int[] ids=(int[])iface.getMethod("getInputDeviceIds").invoke(manager);
        for(int id:ids) {
            Object dev=iface.getMethod("getInputDevice",int.class).invoke(manager,id);
            String name=(String)dev.getClass().getMethod("getName").invoke(dev);
            if(name.equals("vivo_fp")||name.equals("vivo_ts_fp")||name.equals("qbt_key_input")) {
                iface.getMethod("disableInputDevice",int.class).invoke(manager,id);
                System.out.println("Disabled guest phantom mouse: "+name+" id="+id);
            }
        }
    }
}
