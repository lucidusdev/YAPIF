/***
 * CREDIT
 * Original code from PlayIntegrityFix(https://github.com/chiteroman/PlayIntegrityFix)
 * With modifications by lucidusdev to support:
 * - .prop/.json fingerprint definition file.
 * - Customization through yapif.ini.
 *
 * Note:
 * All property handling logic is set in yapif.ini. 
 *
 */
#include "yapif.hpp"

typedef void (*T_Callback)(void *, const char *, const char *, uint32_t);

static T_Callback o_callback = nullptr;

static void modify_callback(void *cookie, const char *name, const char *value, uint32_t serial)
{

    if (cookie == nullptr || name == nullptr || value == nullptr || o_callback == nullptr)
        return;

    std::string prop(name);
    std::string val(value);

    //propVals for exact match, propPostfix for ends_with.
    if (propVals.count(prop))
    {
        val = propVals[prop];
    }
    else
    {
        for (const auto &p : propPostfix)
        {
            if (prop.ends_with(p.first))
            {
                val = p.second;
                break;
            }
        }
    }

    // always  log changed hooked calls
    if (std::string_view(value) != val)
        LOGD("[%s]: %s -> %s", name, value, val.c_str());
    else if (logLevel >= 2 && !prop.starts_with("cache_") && !prop.starts_with("debug_"))
    {
        LOGD("[%s] == %s", name, value);
    }
    return o_callback(cookie, name, val.c_str(), serial);
}

static void (*o_system_property_read_callback)(const prop_info *, T_Callback, void *);

static void
my_system_property_read_callback(const prop_info *pi, T_Callback callback, void *cookie)
{
    if (pi == nullptr || callback == nullptr || cookie == nullptr)
    {
        return o_system_property_read_callback(pi, callback, cookie);
    }
    o_callback = callback;
    return o_system_property_read_callback(pi, modify_callback, cookie);
}

static void doHook()
{
    void *handle = DobbySymbolResolver(nullptr, "__system_property_read_callback");
    if (handle == nullptr)
    {
        LOGD("Couldn't find '__system_property_read_callback' handle. Report to @chiteroman");
        return;
    }
    LOGD("Found '__system_property_read_callback' handle at %p", handle);
    DobbyHook(handle, (dobby_dummy_func_t)my_system_property_read_callback,
              (dobby_dummy_func_t *)&o_system_property_read_callback);
}
// load evertying inside a single vector<char> to r/w only once.
static void companion(int fd)
{
    // |--long long long--|--config--|--prop--|--classes.dex--|
    std::vector<char> data(sizeof(long) * 3, 0);
    const std::string BASE = PIF_FILE_BASE;

    // load yapif.ini
    long size = PIFProp::appendLoad(BASE + "yapif.ini", data);
    memcpy(data.data(), &size, sizeof(long));
    // parse
    PIFProp::ini conf;
    conf.parse(data.data() + sizeof(long) * 3, size);
    auto propFiles = conf.get("MAIN", "PROP_FILES",
                              {"custom.pif.json", "custom.pif.prop", "pif.json", "pif.prop"});
    conf.clear();
    // auto propFiles = {"custom.pif.json", "custom.pif.prop", "pif.json", "pif.prop"};

    // load pif.prop
    std::string propPath;
    for (const auto &s : propFiles)
    {
        propPath = s.starts_with("/") ? s : BASE + s;
        if (std::filesystem::exists(propPath))
        {
            if (std::filesystem::is_symlink(propPath))
            {
                LOGD("Use linked prop: %s->%s", s.c_str(),
                     std::filesystem::read_symlink(propPath).c_str());
            }
            else
            {
                LOGD("Use prop file:  %s", s.c_str());
            }
            break;
        }
        else
        {
            propPath = "";
        }
    }
    if (!propPath.empty())
    {
        size = PIFProp::appendLoad(propPath, data);
        memcpy(data.data() + sizeof(long), &size, sizeof(long));
    }

    // load classes.dex
    size = PIFProp::appendLoad(BASE + "classes.dex", data);
    memcpy(data.data() + sizeof(long) * 2, &size, sizeof(long));

    write(fd, data.data(), data.size());
    std::vector<char>().swap(data);
}

REGISTER_ZYGISK_MODULE(YAPIF)

REGISTER_ZYGISK_COMPANION(companion)