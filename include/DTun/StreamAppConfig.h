#ifndef _DTUN_STREAMAPPCONFIG_H_
#define _DTUN_STREAMAPPCONFIG_H_

#include "DTun/Types.h"
#include "DTun/AppConfig.h"

#include <boost/property_tree/ptree.hpp>

#include <istream>

namespace DTun
{
    class StreamAppConfig : public AppConfig
    {
    public:
        StreamAppConfig();
        ~StreamAppConfig();

        bool load(std::istream& is);

        virtual std::vector<std::string> getSubKeys(const std::string& key = "") const;

        virtual bool isPresent(const std::string& key) const;

        virtual std::string getString(const std::string& key) const;

        virtual int getStringIndex(const std::string& key,
            const std::vector<std::string>& allowed) const;

        virtual UInt32 getUInt32(const std::string& key,
            UInt32 lower = std::numeric_limits<UInt32>::min(),
            UInt32 upper = std::numeric_limits<UInt32>::max()) const;

        virtual SInt32 getSInt32(const std::string& key,
            SInt32 lower = std::numeric_limits<SInt32>::min(),
            SInt32 upper = std::numeric_limits<SInt32>::max()) const;

        virtual double getDouble(const std::string& key,
            double lower = -std::numeric_limits<double>::max(),
            double upper = std::numeric_limits<double>::max()) const;

        virtual bool getBool(const std::string& key) const;

    private:
        boost::optional<const boost::property_tree::ptree&> findKey(const std::string& key) const;

        template <class T>
        T getNumeric(const char* typeName,
            const std::string& key,
            T lower,
            T upper) const;

        boost::property_tree::ptree tree_;
    };
}

#endif
