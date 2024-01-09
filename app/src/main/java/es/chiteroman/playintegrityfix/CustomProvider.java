package es.chiteroman.playintegrityfix;

import java.security.Provider;

public class CustomProvider extends Provider {

    protected CustomProvider(Provider provider) {
        super(provider.getName(), provider.getVersion(), provider.getInfo());

        putAll(provider);

        remove("KeyStore.AndroidKeyStore");

        put("KeyStore.AndroidKeyStore", CustomKeyStoreSpi.class.getName());
    }

    @Override
    public synchronized Service getService(String type, String algorithm) {
        EntryPoint.LOG3("[SERVICE] Type: " + type + " | Algorithm: " + algorithm);
        //EntryPoint.spoofDevice();
        return super.getService(type, algorithm);
    }
}