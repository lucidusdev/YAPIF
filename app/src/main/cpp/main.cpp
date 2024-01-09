#include <android/log.h>
#include <sys/system_properties.h>
#include <unistd.h>


#include "zygisk.hpp"
#include "dobby.h"

#include "pifprop.hpp"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "PIF/Native", __VA_ARGS__)

#define PIF_FILE_BASE "/data/adb/modules/yapif/"
#define PIF_CONFIG_FILE "/data/adb/modules/yapif/yapif.ini"


static std::map<std::string, std::string> propVals, propPostfix;
static std::vector<std::string> propList;
static int logLevel;

typedef void (*T_Callback)(void *, const char *, const char *, uint32_t);

static T_Callback o_callback = nullptr;

static void modify_callback(void *cookie, const char *name, const char *value, uint32_t serial) {

    if (cookie == nullptr || name == nullptr || value == nullptr || o_callback == nullptr)
        return;


    std::string prop(name);
    std::string val(value);
    //only hook ro.board.first_api_level ro.product.first_api_level
    //ro.vendor.api_level is cacluated as minimal val, so leave it untouched??
    //or explicitly set with "*.api_level"??
    //need test on newer devices
    //https://source.android.com/docs/core/tests/vts/setup11#requiring-min-api-level

    //ID, SECURITY_PATCH etc.. are still in propVals but unused. All things done through raw prop maps.

    if (propVals.count(prop)) {
        val = propVals[prop];
    } else {
        for (const auto &p: propPostfix) {
            if (prop.ends_with(p.first)) {
                //if (p.first.starts_with(".") && prop.ends_with(p.first)) {
                val = p.second;
                break;
            }
        }
    }

    //always  log changed hooked calls
    if (std::string_view(value) != val)
        LOGD("[%s]: %s -> %s", name, value, val.c_str());
    else if (logLevel >= 2 && !prop.starts_with("cache_")) {
        LOGD("[%s] == %s", name, value);
    }
    return o_callback(cookie, name, val.c_str(), serial);
}

static void (*o_system_property_read_callback)(const prop_info *, T_Callback, void *);

static void
my_system_property_read_callback(const prop_info *pi, T_Callback callback, void *cookie) {
    if (pi == nullptr || callback == nullptr || cookie == nullptr) {
        return o_system_property_read_callback(pi, callback, cookie);
    }
    o_callback = callback;
    return o_system_property_read_callback(pi, modify_callback, cookie);
}

static void doHook() {
    void *handle = DobbySymbolResolver(nullptr, "__system_property_read_callback");
    if (handle == nullptr) {
        LOGD("Couldn't find '__system_property_read_callback' handle. Report to @chiteroman");
        return;
    }
    LOGD("Found '__system_property_read_callback' handle at %p", handle);
    DobbyHook(handle, (dobby_dummy_func_t) my_system_property_read_callback,
              (dobby_dummy_func_t *) &o_system_property_read_callback);
}

class PlayIntegrityFix : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *_api, JNIEnv *_env) override {
        this->api = _api;
        this->env = _env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        bool isGms = false, isGmsUnstable = false;

        auto process = env->GetStringUTFChars(args->nice_name, nullptr);

        if (process) {
            isGms = strncmp(process, "com.google.android.gms", 22) == 0;
            isGmsUnstable = strcmp(process, "com.google.android.gms.unstable") == 0;
        }

        env->ReleaseStringUTFChars(args->nice_name, process);

        if (!isGms) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        api->setOption(zygisk::FORCE_DENYLIST_UNMOUNT);

        if (!isGmsUnstable) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        int fd = api->connectCompanion();

        std::vector<char> data;

        //yapif.ini pif.prop classes.dex
        // |--longx3--|--config--|--prop--|--classes.dex--|
        PIFProp::appendLoad(fd, data);
        close(fd);
        long configSize, dexSize, propSize;

        if (data.size() < sizeof(long) * 3) {
            LOGD("Error!!! Load %zu bytes from companion!", data.size());
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            std::vector<char>().swap(data);
            return;
        }

        memcpy(&configSize, data.data(), sizeof(long));
        memcpy(&propSize, data.data() + sizeof(long), sizeof(long));
        memcpy(&dexSize, data.data() + sizeof(long) * 2, sizeof(long));
        if (configSize <= 0 || propSize <= 0 || dexSize <= 0 ||
            configSize + propSize + dexSize + sizeof(long) * 3 != data.size()) {
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

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        if (dexVector.empty() || prop.empty()) return;

        parseProp();

        doHook();

        inject();

        std::vector<char>().swap(dexVector);
        prop.clear();
        config.clear();
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *args) override {
        api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }

private:
    zygisk::Api *api = nullptr;
    JNIEnv *env = nullptr;
    std::vector<char> dexVector;
    PIFProp::prop prop;
    PIFProp::ini config;

    bool parseProp() {
        LOGD("Prop contains %zu keys!", prop.items().size());
        //json (multi possible) field name(s) -> propVals key
        propVals.clear();
        propPostfix.clear();

        std::vector<std::string> eraseKeys;
        std::string keyName;
        auto bNameMap = config.get("MAIN", "NAMEMAP", 1);
        auto nameMap = config.getSection("NAMEMAP");

        //1. user defined
        //2. auto fill missing props from fingerprint.
        //3. java props to raw prop
        //4. preset raw props
        //5. resetprop
        for (auto &p: prop.items()) {
            keyName = p.first;

            if (p.first.find_first_of(".*") ==
                std::string::npos) { //does NOT contain dot or asterisk, java prop
                //multi api keys -> single key map. backwards compatibility
                if (bNameMap && nameMap.count(p.first))
                    keyName = nameMap.at(p.first);
            }

            if (keyName.starts_with("*")) {
                propPostfix[keyName.substr(1)] = p.second;
            } else {
                propVals[keyName] = p.second;
            }
        }

        logLevel = propVals.count("LOG_LEVEL") ? stoi(propVals["LOG_LEVEL"]) :
                   config.get("MAIN", "LOG_LEVEL", 0);
        if (logLevel >= 1) {
            //set LOG_LEVEL for java.
            propVals["LOG_LEVEL"] = std::to_string(logLevel);
        }
        LOGD("LOG LEVEL: %d", logLevel);

        autoFillProp();

        //generate prop from propVals for java
        prop.clear();
        for (auto &p: propVals) {
            if (p.first.find(".") == std::string::npos)
                prop.set(p.first, p.second);
        }

        if (logLevel >= 3) {
            for (const auto &p: propVals)
                LOGD("propVals %s : %s", p.first.c_str(), p.second.c_str());
            for (const auto &p: propPostfix)
                LOGD("propPostfix %s : %s", p.first.c_str(), p.second.c_str());
            LOGD("JAVA Props:\n%s", prop.dump().c_str());
        }

        return true;
    }

    void autoFillProp() {
        //MAIN->AUTOPROP : auto fill missing BRAND DEVICE etc..
        //MAIN->PROPMAP1 : Java prop -> raw prop map
        //MAIN->PROPMAP2 : Extra Java prop -> raw prop map

        if (config.get("MAIN", "AUTOPROP", 1) && propVals.count("FINGERPRINT")) {
            const std::vector<std::string> PROPS = {"BRAND", "PRODUCT", "DEVICE", "RELEASE",
                                                    "ID",
                                                    "INCREMENTAL", "TYPE", "TAGS"};
            auto splitFP = PIFProp::split(propVals["FINGERPRINT"], ":/");
            if (splitFP.size() == 8) {
                for (auto i = 0; i < 8; i++) {
                    if (propVals.count(PROPS[i]))
                        continue;
                    propVals[PROPS[i]] = splitFP[i];
                    LOGD("Auto prop: %s -> %s", PROPS[i].c_str(), splitFP[i].c_str());
                }
            }
        }


        std::map<std::string, std::string> j2r;
        std::map<std::string, std::string> propMap;

        for (auto &s: {"PROPMAP1", "PROPMAP2"}) {
            if (config.get("MAIN", s, 1))
                j2r.insert(config.getSection(s).begin(),
                           config.getSection(s).end());
        }

        if (logLevel >= 3)
            LOGD("j2r map size: %zu", j2r.size());
        //add missing raw props from JAVA
        //JAVA prop -> raw prop
        //p.first : java prop name; propVals[p.first]: defined value in pif.prop
        //p.second: raw property name for the java prop.

        for (auto &p: j2r) {
            if (!propVals.count(p.first))
                continue;
            std::vector<std::string> v = PIFProp::split(p.second, ",", true);
            for (auto &j: v) {
                propMap.insert(std::make_pair(j, propVals[p.first]));
            }
        }


        //extraprop + resetprop
        for (auto &s: {"EXTRAPROP", "RESETPROP"}) {
            if (config.get("MAIN", s, 1))
                propMap.insert(config.getSection(s).begin(),
                               config.getSection(s).end());
        }

        for (auto &p: propMap) {
            if (p.first.starts_with("*"))
                propPostfix.insert(std::make_pair(p.first.substr(1), p.second));
            else
                propVals.insert(p);
        }
        j2r.clear();
        propMap.clear();
    }

    void inject() {
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

        auto entryClass = (jclass) entryClassObj;

        LOGD("call init");
        auto entryInit = env->GetStaticMethodID(entryClass, "init", "(Ljava/lang/String;)V");
        auto javaStr = env->NewStringUTF(prop.dump().c_str());
        env->CallStaticVoidMethod(entryClass, entryInit, javaStr);
    }

};

static void companion(int fd) {
    // |--longx3--|--config--|--prop--|--classes.dex--|
    std::vector<char> data(sizeof(long) * 3, 0);
    const std::string BASE = PIF_FILE_BASE;

    //load yapif.ini
    long size = PIFProp::appendLoad(BASE + "yapif.ini", data);
    memcpy(data.data(), &size, sizeof(long));
    //parse
    PIFProp::ini conf;
    conf.parse(data.data() + sizeof(long) * 3, size);
    auto propFiles = conf.get("MAIN", "PROP_FILES",
                              {"custom.pif.json", "custom.pif.prop", "pif.json", "pif.prop"});
    conf.clear();
    //auto propFiles = {"x.pif.json", "x.pif.prop", "custom.pif.json", "custom.pif.prop", "pif.json", "pif.prop"};

    //load pif.prop
    std::string propPath;
    for (const auto &s: propFiles) {
        propPath = s.starts_with("/") ? s : BASE + s;
        if (std::filesystem::exists(propPath)) {
            if (std::filesystem::is_symlink(propPath)) {
                LOGD("Use linked prop: %s->%s", s.c_str(),
                     std::filesystem::read_symlink(propPath).c_str());
            } else {
                LOGD("Use prop file:  %s", s.c_str());
            }
            break;
        } else {
            propPath = "";
        }
    }
    if (!propPath.empty()) {
        size = PIFProp::appendLoad(propPath, data);
        memcpy(data.data() + sizeof(long), &size, sizeof(long));
    }

    //load classes.dex
    size = PIFProp::appendLoad(BASE + "classes.dex", data);
    memcpy(data.data() + sizeof(long) * 2, &size, sizeof(long));

    write(fd, data.data(), data.size());
    std::vector<char>().swap(data);
}

REGISTER_ZYGISK_MODULE(PlayIntegrityFix)

REGISTER_ZYGISK_COMPANION(companion)