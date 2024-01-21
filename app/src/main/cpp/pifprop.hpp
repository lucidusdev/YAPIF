/***
 * A prop/json/ini file parser for YAPIF(Yet Another Play Integrity Fix) Project.
 *
 * *******************************************************
 * PIFProp::prop
 * A simple prop file parser class for Play Integrity Fix Module
 * For prop file, it can be in java .prop or flat(non-nested) json format.
 * Any line starts with "/", "#", "{", "}" is comment.
 * 
 * json:
 *
 * *********************
 * {
 * //this is a comment line
 *      "PROP1" : "Value1",
 *      "PROP2" : "Value 2"
 * }
 * *********************
 * 
 * prop:
 * 
 * *********************
 * # comment
 *      PROP1 : Value1
 *      PROP2 = Value2
 * *********************
 * KNOWN ISSUES:
 * - can't handle escape chars
 * - prop key/value ending with '"' ',' will get trimmed
 * - each key/value, must be in separated line
 *
 * *******************************************************
 *
 * PIFProp::ini
 * A simple ini config file parser for yapif.ini.
 * Accessing values using ini.get(section, key, defaultvalue)
 * Any line starts with ";", "#" is comment.
 * 
 * ini:
 * 
 * *********************
 * ; This is the main section.
 * [MAIN]
 * config1=value1
 * config2=value2
 * 
 * [SETTING1]
 * config3=value3
 * config4=value4
 * *********************
 * 
 * by lucidusdev
 *
 */
#pragma once
#include <string>
#include <map>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <fcntl.h>

namespace PIFProp
{

    const std::string WHITESPACE = " \t\n\r\f\v\",";

    /// <summary>
    /// Load file by fd, add binary content to the end of std::vector<char>
    /// </summary>
    /// <param name="fd">file descriptor, from open()</param>
    /// <param name="data">vector<char> for file content</param>
    /// <returns>count of bytes loaded</returns>
    long appendLoad(int fd, std::vector<char> &data)
    {
        auto oldSize = data.size();
        if (fd <= 0)
            return 0;
        char buff[4096];
        size_t count;
        while ((count = read(fd, buff, 4096)) > 0)
        {
            data.insert(data.end(), buff, buff + count);
        }
        return static_cast<long>(data.size()) - static_cast<long>(oldSize);
    };

    /// <summary>
    /// Load file by path, add binary content to the end of std::vector<char>
    /// </summary>
    /// <param name="path">File name</param>
    /// <param name="data">vector<char> for file content</param>
    /// <returns>count of bytes loaded</returns>
    long appendLoad(const std::string &path, std::vector<char> &data)
    {
        int fd = open(path.c_str(), O_RDONLY);
        long ret = 0;
        if (fd >= 0)
        {
            ret = appendLoad(fd, data);
            close(fd);
        }
        return ret;
    };

    /// <summary>
    /// Remove white space from left, right side of string and return trimed string.
    /// </summary>
    /// <param name="str">String to be trimed</param>
    /// <param name="trimVal">For split function to control trim</param>
    /// <param name="whitespace">white space chars to be removed</param>
    /// <returns>trimed string</returns>
    std::string trim_copy(const std::string &str, bool trimVal = true, const std::string &whitespace = WHITESPACE)
    {
        if (!trimVal)
            return str;
        size_t first = str.find_first_not_of(whitespace);
        if (first == std::string::npos)
        {
            return str;
        }
        size_t last = str.find_last_not_of(whitespace);
        return str.substr(first, (last - first + 1));
    }

    /// <summary>
    /// Split string into key/value pair by first available delimters, usually ":="
    /// </summary>
    /// <param name="str">String to be splitted into 2 parts</param>
    /// <param name="delimiters">String contains all possible delimters, use first avail. one</param>
    /// <returns></returns>
    std::vector<std::string> splitKV(const std::string &str, const std::string &delimiters, const bool trimVal = true, const std::string &whitespace = WHITESPACE)
    {
        std::vector<std::string> res;
        auto pos = str.find_first_of(delimiters, 0);
        if (pos != std::string::npos)
        {
            res.push_back(trim_copy(str.substr(0, pos), trimVal, whitespace));
            res.push_back(trim_copy(str.substr(pos + 1), trimVal, whitespace));
        }
        return res;
    }

    /// <summary>
    /// Split string by multi possible delimters
    /// </summary>
    /// <param name="str">String to be splitted</param>
    /// <param name="delimiters">String contains all possible delimters</param>
    /// <returns>string vector</returns>
    std::vector<std::string> split(const std::string &str, const std::string &delimiters, const bool trimVal = false, const std::string &whitespace = WHITESPACE)
    {
        std::vector<std::string> res;
        size_t pos_start = 0, pos_end;
        while ((pos_end = str.find_first_of(delimiters, pos_start)) !=
               std::string::npos)
        {
            res.push_back(trim_copy(str.substr(pos_start, pos_end - pos_start), trimVal, whitespace));
            pos_start = pos_end + 1;
        }
        res.push_back(trim_copy(str.substr(pos_start), trimVal, whitespace));
        return res;
    }

    class prop
    {
    private:
        std::map<std::string, std::string> map;

    public:
        prop(){};
        ~prop(){};

        /// <summary>
        /// Load prop with vector<char>
        /// </summary>
        /// <param name="data">Input vector<char></param>
        /// <param name="allowEmptyVal">Allow empty value to be set in prop</param>
        // void parse(std::string str, const bool allowEmptyVal = false) {
        void parse(char *buf, long size, const bool allowEmptyVal = false)
        {
            map.clear();
            std::string str(buf, size);
            std::vector<std::string> lines = split(str, "\n");

            for (const auto &_line : lines)
            {
                std::string line = trim_copy(_line);
                if (line.find_first_of("#{}/") == 0 || line.empty())
                    continue;
                auto kv = splitKV(line, "=:", true);
                if (kv.size() != 2)
                    continue;
                if (allowEmptyVal || !kv[1].empty())
                    map[kv[0]] = kv[1];
            }
        }

        /// <summary>
        /// Output prop as key=val strings.
        /// </summary>
        /// <returns></returns>
        std::string dump()
        {
            std::string res;
            for (const auto &kv : map)
            {
                res += kv.first + "=" + kv.second + "\n";
            }
            return res;
        }

        /// <summary>
        /// Get prop value by name
        /// </summary>
        /// <param name="key">prop name</param>
        /// <param name="val">return value if key is not present. default ""</param>
        /// <returns>prop value, empty if key isn't in prop</returns>
        std::string get(const std::string &key, const std::string &val = "")
        {
            return map.count(key) ? map.at(key) : val;
        }

        void set(const std::string &key, const std::string &val, const bool allowEmptyVal = false)
        {
            if (allowEmptyVal || !val.empty())
                map[key] = val;
        }

        /// <summary>
        /// Remove a prop by name. No need to test key existance.
        /// </summary>
        /// <param name="key">prop name</param>
        void erase(const std::string &key)
        {
            map.erase(key);
        }

        void erase(std::vector<std::string> &keys)
        {
            for (const auto &key : keys)
                map.erase(key);
        }

        void clear()
        {
            map.clear();
        }

        bool empty()
        {
            return map.empty();
        }

        const auto &items()
        {
            return map;
        }
    };

    class ini
    {
    private:
        std::map<std::string, std::map<std::string, std::string>> map;

    public:
        ini() {}

        ~ini() {}

        //        void parse(std::string str) {
        void parse(char *buf, long size)
        {
            std::string str(buf, size);
            std::vector<std::string> lines = split(str, "\n");
            std::string section;
            for (const auto &_line : lines)
            {
                std::string line = trim_copy(_line);
                if (line.find_first_of("#;") == 0 || line.empty())
                    continue;
                if (line.starts_with("[") && line.ends_with("]"))
                {
                    section = line.substr(1, line.size() - 2);
                    continue;
                }
                if (!section.empty())
                {
                    auto kv = splitKV(line, "=", true);
                    if (kv.size() != 2)
                        continue;
                    map[section][kv[0]] = kv[1];
                }
            }
        }

        int get(const std::string &section, const std::string &key, const int val)
        {
            return map.count(section) && map[section].count(key) ? stoi(map[section][key]) : val;
        }

        std::string get(const std::string &section, const std::string &key, const std::string &val)
        {
            return map.count(section) && map[section].count(key) ? map[section][key] : val;
        }

        std::vector<std::string> get(const std::string &section, const std::string &key, const std::vector<std::string> &val)
        {
            auto ret = split(get(section, key, ""), ",", true);
            return ret.size() ? ret : val;
        }

        const auto &getSection(const std::string &section)
        {
            return map[section];
        }

        void clear()
        {
            map.clear();
        }
    };
}
