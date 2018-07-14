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

    class DTUN_API LTUDPManager : public SManager
    {
    public:
        explicit LTUDPManager(SManager& mgr);
        ~LTUDPManager();

        virtual SReactor& reactor();

        bool start();

        virtual boost::shared_ptr<SHandle> createStreamSocket();

        virtual boost::shared_ptr<SHandle> createDatagramSocket();

        void addToKill(const boost::shared_ptr<LTUDPHandleImpl>& handle);

        boost::shared_ptr<SConnection> createTransportConnection(const struct sockaddr* name, int namelen);

    private:
        typedef std::set<boost::shared_ptr<LTUDPHandleImpl> > HandleSet;
        typedef std::map<std::pair<UInt32, UInt16>, boost::weak_ptr<SConnection> > ConnectionCache;

        static err_t netifInitFunc(struct netif* netif);

        static err_t netifInputFunc(struct pbuf* p, struct netif* netif);

        static err_t netifOutputFunc(struct netif* netif, struct pbuf* p, const ip4_addr_t* ipaddr);

        void onRecv(int err, int numBytes,
            const boost::weak_ptr<SConnection>& conn,
            const boost::shared_ptr<std::vector<char> >& rcvBuff);

        void onTcpTimeout();

        void onKillHandles(bool sameThreadOnly);

        void reapConnCache();

        SManager& innerMgr_;
        boost::shared_ptr<OpWatch> watch_;
        struct netif netif_;

        boost::mutex m_;
        int numAliveHandles_;
        int tcpTimerMod4_;
        ConnectionCache connCache_;
        HandleSet toKillHandles_;
    };
}

#endif
