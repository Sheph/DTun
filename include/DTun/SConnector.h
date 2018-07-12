#ifndef _DTUN_SCONNECTOR_H_
#define _DTUN_SCONNECTOR_H_

#include "DTun/SHandler.h"
#include <boost/function.hpp>

namespace DTun
{
    class SConnector : public virtual SHandler
    {
    public:
        typedef boost::function<void (int)> ConnectCallback;

        SConnector() {}
        virtual ~SConnector() {}

        virtual bool connect(const std::string& address, const std::string& port, const ConnectCallback& callback, bool rendezvous) = 0;
    };
}

#endif
