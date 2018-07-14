#ifndef _DTUN_SCONNECTION_H_
#define _DTUN_SCONNECTION_H_

#include "DTun/SHandler.h"
#include <boost/function.hpp>

namespace DTun
{
    class DTUN_API SConnection : public virtual SHandler
    {
    public:
        typedef boost::function<void (int)> WriteCallback;
        typedef boost::function<void (int, int)> ReadCallback;
        typedef boost::function<void (int, int, UInt32, UInt16)> ReadFromCallback;

        SConnection() {}
        virtual ~SConnection() {}

        virtual void write(const char* first, const char* last, const WriteCallback& callback) = 0;

        virtual void read(char* first, char* last, const ReadCallback& callback, bool readAll) = 0;

        virtual void writeTo(const char* first, const char* last, UInt32 destIp, UInt16 destPort, const WriteCallback& callback) = 0;

        virtual void readFrom(char* first, char* last, const ReadFromCallback& callback) = 0;
    };
}

#endif
