#ifndef _DTUN_LTUDPHANDLEIMPL_H_
#define _DTUN_LTUDPHANDLEIMPL_H_

#include "DTun/Types.h"
#include "DTun/SConnection.h"
#include <boost/noncopyable.hpp>
#include <lwip/tcp.h>

namespace DTun
{
    class LTUDPManager;

    class LTUDPHandleImpl : boost::noncopyable
    {
    public:
        typedef boost::function<bool (const boost::shared_ptr<SHandle>&)> ListenCallback;

        LTUDPHandleImpl(LTUDPManager& mgr, const boost::shared_ptr<SConnection>& conn);
        ~LTUDPHandleImpl();

        inline LTUDPManager& mgr() { return mgr_; }

        inline const boost::shared_ptr<SConnection>& conn() const { return conn_; }

        void listen(int backlog, const ListenCallback& callback);

    private:
        LTUDPManager& mgr_;
        boost::shared_ptr<SConnection> conn_;
        struct tcp_pcb* pcb_;
        ListenCallback listenCallback_;
    };
}

#endif
