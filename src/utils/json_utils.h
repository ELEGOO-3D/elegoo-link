#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <nlohmann/json.hpp>
namespace elink
{
    /**
     * JSON utility class
     */
    class JsonUtils
    {
    public:
        // ========== JsonUtils Implementation ==========

        static bool isValidJson(const std::string &json_str)
        {
            try
            {
                auto json = nlohmann::json::parse(json_str);
                (void)json; // Explicitly mark as unused to avoid unused variable warning
                return true;
            }
            catch (const std::exception &)
            {
                return false;
            }
        }

        static std::string formatJson(const std::string &json_str)
        {
            try
            {
                auto json_obj = nlohmann::json::parse(json_str);
                return json_obj.dump(4); // 4 spaces indentation
            }
            catch (const std::exception &)
            {
                return json_str;
            }
        }

        static std::string compactJson(const std::string &json_str)
        {
            try
            {
                auto json_obj = nlohmann::json::parse(json_str);
                return json_obj.dump(); // compact format
            }
            catch (const std::exception &)
            {
                return json_str;
            }
        }

        template <typename T>
        static T safeGet(const nlohmann::json &j, const std::string &key, const T &default_value)
        {
            try
            {
                if (j.contains(key))
                {
                    return j.at(key).get<T>();
                }
            }
            catch (...)
            {
                // ignore
            }
            return default_value;
        }

        static int safeGetInt(const nlohmann::json &j, const std::string &key, int default_value)
        {
            try
            {
                if (j.contains(key) && j.at(key).is_number_integer())
                {
                    return j.at(key).get<int>();
                }
            }
            catch (...)
            {
                // ignore
            }
            return default_value;
        }

        static int64_t safeGetInt64(const nlohmann::json &j, const std::string &key, int64_t default_value)
        {
            try
            {
                if (j.contains(key) && j.at(key).is_number_integer())
                {
                    return j.at(key).get<int64_t>();
                }
            }
            catch (...)
            {
                // ignore
            }
            return default_value;
        }

        static std::string safeGetString(const nlohmann::json &j, const std::string &key, const std::string &default_value)
        {
            try
            {
                if (j.contains(key) && j.at(key).is_string())
                {
                    return j.at(key).get<std::string>();
                }
            }
            catch (...)
            {
                // ignore
            }
            return default_value;
        }

        static bool safeGetBool(const nlohmann::json &j, const std::string &key, bool default_value)
        {
            try
            {
                if (j.contains(key) && j.at(key).is_boolean())
                {
                    return j.at(key).get<bool>();
                }
            }
            catch (...)
            {
                // ignore
            }
            return default_value;
        }

        static nlohmann::json safeGetJson(const nlohmann::json &j, const std::string &key, const nlohmann::json &default_value)
        {
            try
            {
                if (j.contains(key) && j.at(key).is_object())
                {
                    return j.at(key);
                }
            }
            catch (...)
            {
                // ignore
            }
            return default_value;
        }

        static double safeGetDouble(const nlohmann::json &j, const std::string &key, double default_value)
        {
            // Support float, int
            try
            {
                if (j.contains(key))
                {
                    if (j.at(key).is_number_float())
                    {
                        return j.at(key).get<double>();
                    }
                    else if (j.at(key).is_number_integer())
                    {
                        return static_cast<double>(j.at(key).get<int>());
                    }
                }
            }
            catch (...)
            {
                // ignore
            }
            return default_value;
        }
    };
}
