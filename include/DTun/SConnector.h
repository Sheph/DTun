#ifndef _DTUN_SCONNECTOR_H_
#define _DTUN_SCONNECTOR_H_

#include "DTun/SHandler.h"
#include <boost/function.hpp>

namespace DTun
{
    class DTUN_API SConnector : public virtual SHandler
    {
    public:
        enum Mode
        {
            ModeNormal = 0,
            ModeRendezvousConn,
            ModeRendezvousAcc
        };

        typedef boost::function<void (int)> ConnectCallback;

        SConnector() {}
        virtual ~SConnector() {}

        virtual bool connect(const std::string& address, const std::string& port, const ConnectCallback& callback, Mode mode) = 0;
    };
}

#endif
