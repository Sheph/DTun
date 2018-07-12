#ifndef _DTUN_SCONNECTION_H_
#define _DTUN_SCONNECTION_H_

#include "DTun/SHandler.h"
#include <boost/function.hpp>

namespace DTun
{
    class SConnection : public virtual SHandler
    {
    public:
        typedef boost::function<void (int)> WriteCallback;
        typedef boost::function<void (int, int)> ReadCallback;

        SConnection() {}
        virtual ~SConnection() {}

        virtual void write(const char* first, const char* last, const WriteCallback& callback) = 0;

        virtual void read(char* first, char* last, const ReadCallback& callback, bool readAll) = 0;
    };
}

#endif
