#ifndef _DTUN_APPCONFIG_H_
#define _DTUN_APPCONFIG_H_

#include "DTun/Types.h"

#include <limits>
#include <vector>

namespace DTun
{
    class DTUN_API AppConfig
    {
    public:
        AppConfig() {}
        virtual ~AppConfig() {}

        /*
         * Gets first level subkeys of a 'key'. For example, if we have config
         * like this:
         *
         * global_key.1=global_value1
         * global_key.2=global_value2
         * global_key3=global_value3
         * [section1]
         * key1.1=a1
         * key1.2=b1
         * key1.3=c1
         * key2.1=a2
         * key2.2=b2
         * key2.3=c2
         * [section2]
         * k2=v2
         *
         * Then getSubKeys("section1") will yield "key1" and "key2".
         * getSubKeys("section1.key1") will yield "1", "2" and "3".
         * getSubKeys() will yield "section1" and "section2".
         * getSubKeys(".") will yield "global_key" and "global_key3".
         * getSubKeys(".global_key") will yield "1" and "2".
         */
        virtual std::vector<std::string> getSubKeys(const std::string& key = "") const = 0;

        /*
         * Returns true if final key 'key' is present in app config. For the
         * config above we'll have:
         *
         * isPresent("section1") == false
         * isPresent("section1.key1.1") == true
         * isPresent("") == false
         * isPresent(".global_key3") == true
         */
        virtual bool isPresent(const std::string& key) const = 0;

        /*
         * Getters for keys. 'key' is final key as described above. These
         * functions write to log if:
         * + 'key' is not found.
         * + 'key' cannot be converted to the specified data type.
         * + 'key' is outside of specified boundaries.
         * @{
         */

        virtual std::string getString(const std::string& key) const = 0;

        virtual int getStringIndex(const std::string& key,
            const std::vector<std::string>& allowed) const = 0;

        virtual UInt32 getUInt32(const std::string& key,
            UInt32 lower = std::numeric_limits<UInt32>::min(),
            UInt32 upper = std::numeric_limits<UInt32>::max()) const = 0;

        virtual SInt32 getSInt32(const std::string& key,
            SInt32 lower = std::numeric_limits<SInt32>::min(),
            SInt32 upper = std::numeric_limits<SInt32>::max()) const = 0;

        virtual double getDouble(const std::string& key,
            double lower = -std::numeric_limits<double>::max(),
            double upper = std::numeric_limits<double>::max()) const = 0;

        /*
         * true values are - "yes", "true", "1", "y".
         * false values are - "no", "false", "0", "n".
         * Others are considered as error.
         */
        virtual bool getBool(const std::string& key) const = 0;

        /*
         * @}
         */
    };
}

#endif
