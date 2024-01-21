#pragma  once


#include <android/log.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include "zygisk.hpp"
#include "dobby.h"

#include "pifprop.hpp"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "PIF/Native", __VA_ARGS__)

#define PIF_FILE_BASE "/data/adb/modules/yapif/"

static std::map<std::string, std::string> propVals, propPostfix;
static int logLevel;

static void doHook();

class YAPIF : public zygisk::ModuleBase
{
public:
    void onLoad(zygisk::Api *_api, JNIEnv *_env) override
    {
        this->api = _api;
        this->env = _env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override
    {
        bool isGms = false, isGmsUnstable = false;

        auto process = env->GetStringUTFChars(args->nice_name, nullptr);

        if (process)
        {
            std::string_view sv(process);
            isGms = sv.starts_with("com.google.android.gms");
            isGmsUnstable = sv == "com.google.android.gms.unstable";
        }

        env->ReleaseStringUTFChars(args->nice_name, process);

        if (!isGms)
        {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        api->setOption(zygisk::FORCE_DENYLIST_UNMOUNT);

        if (!isGmsUnstable)
        {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        int fd = api->connectCompanion();

        std::vector<char> data;

        // yapif.ini pif.prop classes.dex
        //  |--longx3--|--config--|--prop--|--classes.dex--|
        PIFProp::appendLoad(fd, data);
        close(fd);
        long configSize, dexSize, propSize;

        if (data.size() < sizeof(long) * 3)
        {
            LOGD("Error!!! Load %zu bytes from companion!", data.size());
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            std::vector<char>().swap(data);
            return;
        }

        memcpy(&configSize, data.data(), sizeof(long));
        memcpy(&propSize, data.data() + sizeof(long), sizeof(long));
        memcpy(&dexSize, data.data() + sizeof(long) * 2, sizeof(long));
        if (configSize <= 0 || propSize <= 0 || dexSize <= 0 ||
            configSize + propSize + dexSize + sizeof(long) * 3 != data.size())
        {
            LOGD("Error!!! Read files: yapif.ini:%ld pif.prop:%ld classes.dex:%ld, total: %zu bytes",
                 configSize, propSize, dexSize, data.size());
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            std::vector<char>().swap(data);
            return;
        }
        LOGD("Load %zu bytes total from companion!", data.size());
        LOGD("Read files: yapif.ini:%ld pif.prop:%ld classes.dex:%ld bytes",
             configSize, propSize, dexSize);

        // |--longx3--|--config--|--prop--|--classes.dex--|
        config.parse(data.data() + sizeof(long) * 3, configSize);
        prop.parse(data.data() + sizeof(long) * 3 + configSize, propSize);
        dexVector.assign(data.end() - dexSize,
                         data.end());
        std::vector<char>().swap(data);
        return;
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override
    {
        if (dexVector.empty() || prop.empty())
            return;

        parseProp();

        doHook();

        inject();

        std::vector<char>().swap(dexVector);
        prop.clear();
        config.clear();
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *args) override
    {
        api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }

private:
    zygisk::Api *api = nullptr;
    JNIEnv *env = nullptr;
    std::vector<char> dexVector;
    PIFProp::prop prop;
    PIFProp::ini config;


    std::string javaNameConvert(std::string name)
    {
        return config.get("MAIN", "NAMEMAP", 1) ? config.get("NAMEMAP", name, name) : name;
    }

    bool parseProp()
    {
        LOGD("Prop contains %zu keys!", prop.items().size());
        // json (multi possible) field name(s) -> propVals key
        propVals.clear();
        propPostfix.clear();

        std::vector<std::string> eraseKeys;
        std::string keyName;

        // 1. user defined
        // 2. auto fill missing props from fingerprint.
        // 3. java props to raw prop
        // 4. preset raw props
        // 5. spoofprop section

        // 1. user defined
        for (auto &p : prop.items())
        {
            keyName = p.first;

            if (p.first.find_first_of(".*") == std::string::npos)
            { // does NOT contain dot or asterisk, java prop
                // multi api keys -> single key map. backwards compatibility
                keyName = javaNameConvert(p.first);
            }

            if (keyName.starts_with("*"))
            {
                propPostfix[keyName.substr(1)] = p.second;
            }
            else
            {
                propVals[keyName] = p.second;
            }
        }

        // use LOG_LEVEL from yapif.ini with a lower priority,
        // so user don't need to set it in every prop.
        logLevel = propVals.count("LOG_LEVEL") ? stoi(propVals["LOG_LEVEL"]) : config.get("MAIN", "LOG_LEVEL", 0);
        // set LOG_LEVEL for java.
        propVals["LOG_LEVEL"] = std::to_string(logLevel);

        LOGD("LOG LEVEL: %d", logLevel);

        // 2-5.
        autoFillProp();

        // prepare prop for JAVA.
        prop.clear();
        for (auto &p : propVals)
        {
            if (p.first.find(".") == std::string::npos)
                prop.set(p.first, p.second);
        }

        if (logLevel >= 3)
        {
            for (const auto &p : propVals)
                LOGD("propVals %s : %s", p.first.c_str(), p.second.c_str());
            for (const auto &p : propPostfix)
                LOGD("propPostfix %s : %s", p.first.c_str(), p.second.c_str());
            LOGD("JAVA Props:\n%s", prop.dump().c_str());
        }

        return true;
    }

    void autoFillProp()
    {
        // MAIN->AUTOPROP : auto fill missing BRAND DEVICE etc..
        // MAIN->PROPMAP1 : Java prop -> raw prop map
        // MAIN->PROPMAP2 : Extra Java prop -> raw prop map

        if (config.get("MAIN", "AUTOPROP", 1) && propVals.count("FINGERPRINT"))
        {
            const std::vector<std::string> PROPS = {"BRAND", "PRODUCT", "DEVICE", "RELEASE",
                                                    "ID",
                                                    "INCREMENTAL", "TYPE", "TAGS"};
            auto splitFP = PIFProp::split(propVals["FINGERPRINT"], ":/");
            if (splitFP.size() == 8)
            {
                for (auto i = 0; i < 8; i++)
                {
                    if (propVals.count(PROPS[i]))
                        continue;
                    propVals[PROPS[i]] = splitFP[i];
                    LOGD("Auto prop: %s -> %s", PROPS[i].c_str(), splitFP[i].c_str());
                }
            }
        }

        // handle JAVA prop in USERPROP here:
        if (config.get("MAIN", "USERPROP", 1))
        {
            for (auto &p : config.getSection("USERPROP"))
            {
                if (p.first.find_first_of("*.") == std::string::npos)
                {
                    if (p.first.starts_with("!"))
                        propVals[javaNameConvert(p.first.substr(1))] = p.second;
                    else
                        propVals.insert(std::make_pair(javaNameConvert(p.first), p.second));
                }
            }
        }

        // j2r: java -> raw prop map
        std::map<std::string, std::string> j2r;
        // rawProp: raw prop -> value
        std::map<std::string, std::string> rawProp;

        for (auto &s : {"PROPMAP1", "PROPMAP2"})
        {
            if (config.get("MAIN", s, 1))
                j2r.insert(config.getSection(s).begin(),
                           config.getSection(s).end());
        }

        if (logLevel >= 3)
            LOGD("j2r map size: %zu", j2r.size());
        // add missing raw props from JAVA
        // JAVA prop -> raw prop
        // p.first : java prop name; propVals[p.first]: defined value in pif.prop
        // p.second: raw property name for the java prop.

        for (auto &p : j2r)
        {
            if (!propVals.count(p.first))
                continue;
            std::vector<std::string> v = PIFProp::split(p.second, ",", true);
            for (auto &j : v)
            {
                rawProp.insert(std::make_pair(j, propVals[p.first]));
            }
        }

        // useraprop + spoofprop, only raw prop
        for (auto &s : {"USERPROP", "SPOOFPROP"})
        {
            if (config.get("MAIN", s, 1))
            {
                for (auto &p : config.getSection("USERPROP"))
                {
                    if (p.first.find_first_of(".*") != std::string::npos)
                    {
                        if (p.first.starts_with("!"))
                            rawProp[p.first.substr(1)] = p.second;
                        else
                            rawProp.insert(p);
                    }
                }
            }
        }

        for (auto &p : rawProp)
        {
            if (p.first.starts_with("*"))
                propPostfix.insert(std::make_pair(p.first.substr(1), p.second));
            else
                propVals.insert(p);
        }
        j2r.clear();
        rawProp.clear();
    }

    void inject()
    {
        if (logLevel >= 2)
            LOGD("get system classloader");
        auto clClass = env->FindClass("java/lang/ClassLoader");
        auto getSystemClassLoader = env->GetStaticMethodID(clClass, "getSystemClassLoader",
                                                           "()Ljava/lang/ClassLoader;");
        auto systemClassLoader = env->CallStaticObjectMethod(clClass, getSystemClassLoader);
        if (logLevel >= 2)
            LOGD("create class loader");
        auto dexClClass = env->FindClass("dalvik/system/InMemoryDexClassLoader");
        auto dexClInit = env->GetMethodID(dexClClass, "<init>",
                                          "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
        auto buffer = env->NewDirectByteBuffer(dexVector.data(),
                                               static_cast<jlong>(dexVector.size()));
        auto dexCl = env->NewObject(dexClClass, dexClInit, buffer, systemClassLoader);

        if (logLevel >= 2)
            LOGD("load class");
        auto loadClass = env->GetMethodID(clClass, "loadClass",
                                          "(Ljava/lang/String;)Ljava/lang/Class;");
        auto entryClassName = env->NewStringUTF("es.chiteroman.playintegrityfix.EntryPoint");
        auto entryClassObj = env->CallObjectMethod(dexCl, loadClass, entryClassName);

        auto entryClass = (jclass)entryClassObj;

        LOGD("call init");
        auto entryInit = env->GetStaticMethodID(entryClass, "init", "(Ljava/lang/String;)V");
        auto javaStr = env->NewStringUTF(prop.dump().c_str());
        env->CallStaticVoidMethod(entryClass, entryInit, javaStr);
    }
};

