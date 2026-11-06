import android.util.Xml;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlSerializer;
import java.io.*;
import java.lang.reflect.Method;

/** Migrate the old offline test's Bluetooth disable flag before PackageManager starts. */
public class GuestDataPrepare {
    public static void main(String[] args) throws Exception {
        File source = new File("/data/system/users/0/package-restrictions.xml");
        if (!source.isFile()) return;
        File output = new File(source.getPath() + ".guest-migration");
        boolean changed = false;
        try (FileInputStream in = new FileInputStream(source);
             FileOutputStream out = new FileOutputStream(output)) {
            Method resolve = Xml.class.getDeclaredMethod("resolvePullParser", InputStream.class);
            XmlPullParser parser = (XmlPullParser) resolve.invoke(null, in);
            XmlSerializer writer = Xml.newSerializer();
            writer.setOutput(out, "UTF-8");
            writer.startDocument("UTF-8", true);
            for (int event = parser.getEventType(); event != XmlPullParser.END_DOCUMENT; event = parser.next()) {
                if (event == XmlPullParser.START_TAG) {
                    writer.startTag(null, parser.getName());
                    boolean bt = "pkg".equals(parser.getName()) &&
                        "com.android.bluetooth".equals(parser.getAttributeValue(null, "name"));
                    for (int i = 0; i < parser.getAttributeCount(); i++) {
                        String key = parser.getAttributeName(i), value = parser.getAttributeValue(i);
                        if (bt && "enabled".equals(key) && !"0".equals(value) && !"1".equals(value)) {
                            value = "1";
                            changed = true;
                        }
                        writer.attribute(null, key, value);
                    }
                } else if (event == XmlPullParser.END_TAG) {
                    writer.endTag(null, parser.getName());
                } else if (event == XmlPullParser.TEXT) {
                    writer.text(parser.getText());
                }
            }
            writer.endDocument();
            out.getFD().sync();
        }
        if (changed) {
            // Preserve the original inode, owner, SELinux label, and access mode.
            try (FileInputStream in = new FileInputStream(output);
                 FileOutputStream out = new FileOutputStream(source)) {
                byte[] buffer = new byte[8192];
                int n;
                while ((n = in.read(buffer)) != -1) out.write(buffer, 0, n);
                out.getFD().sync();
            }
            System.out.println("Enabled guest Bluetooth package before framework startup");
        } else System.out.println("Guest Bluetooth package already enabled");
        if (!output.delete()) throw new IOException("Could not remove migration file");
    }
}
