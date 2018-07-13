#ifndef _DTUN_LTUDPMANAGER_H_
#define _DTUN_LTUDPMANAGER_H_

#include "DTun/SManager.h"
#include "DTun/OpWatch.h"
#include <boost/thread/mutex.hpp>
#include <set>
#include <lwip/netif.h>

namespace DTun
{
    class LTUDPHandleImpl;

    class LTUDPManager : public SManager
    {
    public:
        explicit LTUDPManager(SManager& mgr);
        ~LTUDPManager();

        virtual SReactor& reactor();

        bool start();

        virtual boost::shared_ptr<SHandle> createStreamSocket();

        virtual boost::shared_ptr<SHandle> createDatagramSocket();

    private:
        typedef std::set<boost::shared_ptr<LTUDPHandleImpl> > HandleSet;
        typedef std::map<std::pair<UInt32, UInt16>, boost::weak_ptr<SConnection> > ConnectionCache;

        void onTcpTimeout();

        SManager& innerMgr_;
        boost::shared_ptr<OpWatch> watch_;
        struct netif netif_;

        boost::mutex m_;
        int numAliveHandles_;
        HandleSet toKillHandles_;
        ConnectionCache connCache_;
    };
}

#endif
