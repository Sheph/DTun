#include "Logger.h"
#include "DTun/StreamAppConfig.h"

#include <boost/property_tree/ini_parser.hpp>

#include <set>

namespace DTun
{
    static const char* yesStrs[] = {"yes", "true", "1", "y", 0};
    static const char* noStrs[] = {"no", "false", "0", "n", 0};

    static boost::optional<bool> convertToBool(const std::string& value)
    {
        for (const char** i = yesStrs; *i != 0; ++i) {
            if (::strcasecmp(value.c_str(), *i) == 0) {
                return true;
            }
        }

        for (const char** i = noStrs; *i != 0; ++i) {
            if (::strcasecmp(value.c_str(), *i) == 0) {
                return false;
            }
        }

        return boost::optional<bool>();
    }

    template <class T>
    static boost::optional<T> getNumericValue(const boost::property_tree::ptree& tree)
    {
        return tree.get_value_optional<T>();
    }

    template <>
    boost::optional<UInt32> getNumericValue<UInt32>(const boost::property_tree::ptree& tree)
    {
        boost::optional<SInt32> tmp = tree.get_value_optional<SInt32>();

        if (!tmp || (*tmp < 0)) {
            return boost::optional<UInt32>();
        }

        return *tmp;
    }

    StreamAppConfig::StreamAppConfig()
    {
    }

    StreamAppConfig::~StreamAppConfig()
    {
    }

    bool StreamAppConfig::load(std::istream& is)
    {
        try {
            boost::property_tree::ini_parser::read_ini(is, tree_);
            return true;
        } catch (...) {
            return false;
        }
    }

    std::vector<std::string> StreamAppConfig::getSubKeys(const std::string& key) const
    {
        std::vector<std::string> res;

        if (key.empty()) {
            /*
             * Return all section names.
             */

            for (boost::property_tree::ptree::const_iterator it = tree_.begin();
                 it != tree_.end();
                 ++it ) {
                if (it->second.empty()) {
                    continue;
                }

                if (it->first.find('.') == std::string::npos) {
                    /*
                     * Section names with '.' in name are ignored.
                     */

                    res.push_back(it->first);
                }
            }

            return res;
        }

        std::string::size_type pos = key.find('.');

        std::string sectionName;
        std::string path;

        if (pos == std::string::npos) {
            sectionName = key;
        } else {
            sectionName = key.substr(0, pos);
            path = key.substr(pos + 1);
        }

        const boost::property_tree::ptree* section;

        if (sectionName.empty()) {
            /*
             * Walk top level keys and match against 'path'.
             */

            section = &tree_;
        } else {
            /*
             * Walk the specific section and match against 'path'.
             */

            boost::optional<const boost::property_tree::ptree&> tmp =
                tree_.get_child_optional(sectionName);

            if (!tmp || tmp->empty()) {
                /*
                 * No such section or it's a top level key.
                 */

                return res;
            }

            section = &*tmp;
        }

        std::set<std::string> keySet;

        if (!path.empty()) {
            path += '.';
        }

        for (boost::property_tree::ptree::const_iterator it = section->begin();
             it != section->end();
             ++it ) {
            if (!it->second.empty()) {
                continue;
            }

            if (it->first.compare(0, path.length(), path) != 0) {
                continue;
            }

            pos = it->first.find('.', path.length());

            std::string tmp;

            if (pos == std::string::npos) {
                tmp = it->first.substr(path.length());
            } else {
                tmp = it->first.substr(path.length(), pos - path.length());
            }

            if (tmp.empty()) {
                continue;
            }

            if (keySet.insert(tmp).second) {
                res.push_back(tmp);
            }
        }

        return res;
    }

    bool StreamAppConfig::isPresent(const std::string& key) const
    {
        return !!findKey(key);
    }

    std::string StreamAppConfig::getString(const std::string& key) const
    {
        boost::optional<const boost::property_tree::ptree&> tmp = findKey(key);

        if (!tmp) {
            LOG4CPLUS_WARN(logger(),
                "Configuration value for \"" << key << "\" not found"
                ", defaulting to \"\"");

            return "";
        }

        return tmp->data();
    }

    int StreamAppConfig::getStringIndex(const std::string& key,
        const std::vector<std::string>& allowed) const
    {
        boost::optional<const boost::property_tree::ptree&> tmp = findKey(key);

        if (!tmp) {
            LOG4CPLUS_WARN(logger(),
                "Configuration value for \"" << key << "\" not found"
                ", defaulting to \"" << allowed[0] << "\"");

            return 0;
        }

        for (int i = 0; i < static_cast<int>(allowed.size()); ++i) {
            if (allowed[i] == tmp->data()) {
                return i;
            }
        }

        if (logger().isEnabledFor(log4cplus::WARN_LOG_LEVEL)) {
            std::ostringstream os;

            for (std::vector<std::string>::const_iterator it = allowed.begin();
                 it != allowed.end();) {
                os << "\"" << *it << "\"";

                ++it;

                if (it != allowed.end()) {
                    os << ", ";
                }
            }

            LOG4CPLUS_WARN(logger(),
                "Configuration value for \"" << key << "\" = \""
                << tmp->data() << "\" must be one of these: " << os.str()
                << ", defaulting to \"" << allowed[0] << "\"");
        }

        return 0;
    }

    UInt32 StreamAppConfig::getUInt32(const std::string& key,
        UInt32 lower,
        UInt32 upper) const
    {
        return getNumeric("unsigned integer",
            key,
            lower,
            upper);
    }

    SInt32 StreamAppConfig::getSInt32(const std::string& key,
        SInt32 lower,
        SInt32 upper) const
    {
        return getNumeric("signed integer",
            key,
            lower,
            upper);
    }

    double StreamAppConfig::getDouble(const std::string& key,
        double lower,
        double upper) const
    {
        return getNumeric("double",
            key,
            lower,
            upper);
    }

    bool StreamAppConfig::getBool(const std::string& key) const
    {
        boost::optional<const boost::property_tree::ptree&> tmp = findKey(key);

        if (!tmp) {
            LOG4CPLUS_WARN(logger(),
                "Configuration value for \"" << key << "\" not found"
                ", defaulting to false");

            return false;
        }

        boost::optional<bool> res = convertToBool(tmp->data());

        if (res) {
            return *res;
        }

        LOG4CPLUS_WARN(logger(),
            "Configuration value for \"" << key << "\" = \""
            << tmp->data() << "\" is not a correct boolean"
            ", defaulting to false");

        return false;
    }

    boost::optional<const boost::property_tree::ptree&> StreamAppConfig::findKey(const std::string& key) const
    {
        if (key.empty()) {
            /*
             * Bad key.
             */

            return boost::optional<const boost::property_tree::ptree&>();
        }

        std::string::size_type pos = key.find('.');

        std::string sectionName;
        std::string path;

        if (pos == std::string::npos) {
            sectionName = key;
        } else {
            sectionName = key.substr(0, pos);
            path = key.substr(pos + 1);
        }

        const boost::property_tree::ptree* section;

        if (sectionName.empty()) {
            /*
             * Find in top level keys.
             */

            section = &tree_;
        } else {
            /*
             * Find in specific section.
             */

            boost::optional<const boost::property_tree::ptree&> tmp =
                tree_.get_child_optional(sectionName);

            if (!tmp || tmp->empty()) {
                return boost::optional<const boost::property_tree::ptree&>();
            }

            section = &*tmp;
        }

        boost::property_tree::ptree::const_assoc_iterator it =
            section->find(path);

        if ((it == section->not_found()) || !it->second.empty()) {
            return boost::optional<const boost::property_tree::ptree&>();
        }

        return it->second;
    }

    template <class T>
    T StreamAppConfig::getNumeric(const char* typeName,
        const std::string& key,
        T lower,
        T upper) const
    {
        boost::optional<const boost::property_tree::ptree&> tmp = findKey(key);

        if (!tmp) {
            LOG4CPLUS_WARN(logger(),
                "Configuration value for \"" << key << "\" not found"
                ", defaulting to 0");

            return T();
        }

        boost::optional<T> val = getNumericValue<T>(*tmp);

        if (!val) {
            LOG4CPLUS_WARN(logger(),
                "Configuration value for \"" << key << "\" = \""
                << tmp->data() << "\" cannot be converted to " << typeName
                << ", defaulting to 0");

            return T();
        }

        if (*val < lower) {
            LOG4CPLUS_WARN(logger(),
                "Configuration value for \"" << key << "\" = "
                << *val << " must be >= " << lower
                << ", defaulting to " << lower);
            return lower;
        }

        if (*val > upper) {
            LOG4CPLUS_WARN(logger(),
                "Configuration value for \"" << key << "\" = "
                << *val << " must be <= " << upper
                << ", defaulting to " << upper);
            return upper;
        }

        return *val;
    }
}
