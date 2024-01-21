/***
 * CREDIT
 * Most of the code below is from PlayIntegrityFork repo(https://github.com/osm0sis/PlayIntegrityFork) by osm0sis.
 * With some minor tweaks.
 * 
 * Note:
 * Properly set FIRST_SDK_INT for Android R(11) and older devices.
 * Both FIRST_SDK_INT and DEVICE_INITIAL_SDK_INT, which response to FIRST_API_LEVEL, 
 * are currently unused in DroidGuard, so it's a futureproofing.
 * 
 */
package es.chiteroman.playintegrityfix;

import android.os.Build;
import android.util.Log;

import java.io.StringReader;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.KeyStoreSpi;
import java.security.Provider;
import java.security.Security;
import java.util.HashMap;
import java.util.Map;
import java.util.Properties;

public final class EntryPoint {
    private static final Map<String, String> map = new HashMap<>();
    private static int logLevel = 0;

    public static void init(String propStr) {
        if (readProp(propStr) && spoofProvider()) {
            spoofDevice();
        }
    }

    private static boolean readProp(String propStr) {
        map.clear();
        try {
            Properties p = new Properties();
            p.load(new StringReader(propStr));
            p.forEach((k, v) -> map.put((String) k, (String) v));
            p.clear();
        } catch (Exception e) {
            LOG0("Couldn't read Prop from Zygisk: " + e);
            map.clear();
        }
        return !map.isEmpty();
    }

    private static boolean spoofProvider() {
        final String KEYSTORE = "AndroidKeyStore";

        try {
            Provider provider = Security.getProvider(KEYSTORE);
            KeyStore keyStore = KeyStore.getInstance(KEYSTORE);

            Field f = keyStore.getClass().getDeclaredField("keyStoreSpi");
            f.setAccessible(true);
            CustomKeyStoreSpi.keyStoreSpi = (KeyStoreSpi) f.get(keyStore);
            f.setAccessible(false);

            CustomProvider customProvider = new CustomProvider(provider);
            Security.removeProvider(KEYSTORE);
            Security.insertProviderAt(customProvider, 1);

            LOG0("Spoof KeyStoreSpi and Provider done!");
            return true;
        } catch (KeyStoreException e) {
            LOG0("Couldn't find KeyStore: " + e);
        } catch (NoSuchFieldException e) {
            LOG0("Couldn't find field: " + e);
        } catch (IllegalAccessException e) {
            LOG0("Couldn't change access of field: " + e);
        } catch (Exception e) {
            LOG0("Error while spoofProvider: " + e);
        }
        return false;
    }

    protected static void spoofDevice() {
        for (String key : map.keySet()) {
            //different first_api for >= A12 / <= A11
            String keyName = (key.equals("DEVICE_INITIAL_SDK_INT") && android.os.Build.VERSION.SDK_INT <= Build.VERSION_CODES.R ) ?
                                     "FIRST_SDK_INT" : key ;
            if (keyName.equals("LOG_LEVEL")) {
                logLevel = Integer.parseInt(map.get(key));
                LOG1(String.format("Log Level: %d", logLevel));
            } else {
                setField(keyName, map.get(key));
            }
        }
        LOG0("Spoof Device done!");

    }

    private static boolean classContainsField(Class className, String fieldName) {
        for (Field field : className.getDeclaredFields()) {
            if (field.getName().equals(fieldName)) return true;
        }
        return false;
    }

    private static void setField(String name, String value) {
        if (value.isEmpty()) {
            LOG1(String.format("%s is empty, skipping...", name));
            return;
        }

        Field field = null;
        String oldValue = null;
        Object newValue = null;

        try {
            if (classContainsField(Build.class, name)) {
                field = Build.class.getDeclaredField(name);
            } else if (classContainsField(Build.VERSION.class, name)) {
                field = Build.VERSION.class.getDeclaredField(name);
            } else {
                LOG1(String.format("Couldn't determine '%s' class name", name));
                return;
            }
        } catch (NoSuchFieldException e) {
            LOG0(String.format("Couldn't find '%s' field name: " + e, name));
            return;
        }
        field.setAccessible(true);
        try {
            oldValue = String.valueOf(field.get(null));
        } catch (IllegalAccessException e) {
            LOG0(String.format("Couldn't access '%s' field value: " + e, name));
            return;
        }
        if (value.equals(oldValue)) {
            LOG1(String.format("[%s]: already '%s', skipping...", name, value));
            return;
        }
        Class<?> fieldType = field.getType();
        if (fieldType == String.class) {
            newValue = value;
        } else if (fieldType == int.class) {
            newValue = Integer.parseInt(value);
        } else if (fieldType == long.class) {
            newValue = Long.parseLong(value);
        } else if (fieldType == boolean.class) {
            newValue = Boolean.parseBoolean(value);
        } else {
            LOG0(String.format("Couldn't convert '%s' to '%s' type", value, fieldType));
            return;
        }
        try {
            field.set(null, newValue);
        } catch (IllegalAccessException e) {
            LOG0(String.format("Couldn't modify '%s' field value: " + e, name));
            return;
        }
        field.setAccessible(false);
        LOG0(String.format("[%s]: %s -> %s", name, oldValue, value));
    }

    protected static void _PIFLOG(String msg, int level) {
        if (logLevel >= level)
            Log.d("PIF/Java", msg);
    }

    protected static void LOG0(String msg) {
        _PIFLOG(msg, 0);
    }

    protected static void LOG1(String msg) {
        _PIFLOG(msg, 1);
    }

    protected static void LOG2(String msg) {
        _PIFLOG(msg, 2);
    }

    protected static void LOG3(String msg) {
        _PIFLOG(msg, 3);
    }


/* trying to hook getFlashLockState()->1 so we avoid resetprop...
   can't get it working. leave here for reference.
    private static void spoofLockedBL() {
        try {
            Class<?> pdbm = Class.forName("android.service.persistentdata.PersistentDataBlockManager");
            if (logLevel >= 2) {
                Field[] declaredFields = pdbm.getDeclaredFields();
                for (Field f : declaredFields) {
                    if (f.getName().startsWith("FLASH_LOCK"))
                        LOG2("Field name: " + f.getName() + "->" + f.get(null));
                    else
                        LOG2("Field name: " + f.getName());
                }
            }

            Field field = pdbm.getDeclaredField("FLASH_LOCK_UNLOCKED");
            LOG3("Found field FLASH_LOCK_UNLOCKED");
            Field accessFlagsField = Field.class.getDeclaredField("accessFlags");
            LOG3("Found modifiers");
            accessFlagsField.setAccessible(true);
            accessFlagsField.setInt(field, field.getModifiers() & ~Modifier.FINAL);
            LOG3("remove final modifiers");
            field.set(null, 1);
            LOG2("New FLASH_LOCK_UNLOCKED -> " + field.get(null));

        } catch (NoSuchFieldException e) {
            LOG0("NoSuchFieldException");
        } catch (ClassNotFoundException e) {
            LOG0("ClassNotFoundException");
        } catch (IllegalAccessException e) {
            LOG0("IllegalAccessException");
        }
    }

*/
}