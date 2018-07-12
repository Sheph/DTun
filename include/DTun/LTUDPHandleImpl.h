#ifndef _DTUN_LTUDPHANDLEIMPL_H_
#define _DTUN_LTUDPHANDLEIMPL_H_

#include "DTun/Types.h"
#include "DTun/SConnection.h"
#include <boost/noncopyable.hpp>
#include <lwip/tcp.h>

namespace DTun
{
    class LTUDPHandleImpl : boost::noncopyable
    {
    public:
        explicit LTUDPHandleImpl(const boost::shared_ptr<SConnection>& conn);
        ~LTUDPHandleImpl();

    private:
        boost::shared_ptr<SConnection> conn_;
        struct tcp_pcb* pcb_;
    };
}

#endif
