#ifndef _DTUN_LTUDPMANAGER_H_
#define _DTUN_LTUDPMANAGER_H_

#include "DTun/SManager.h"
#include "DTun/OpWatch.h"
#include <boost/thread/mutex.hpp>
#include <set>
#include <lwip/netif.h>

struct tcp_pcb;

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

        virtual boost::shared_ptr<SHandle> createDatagramSocket(SYSSOCKET s = SYS_INVALID_SOCKET);

        void addToKill(const boost::shared_ptr<LTUDPHandleImpl>& handle);

        boost::shared_ptr<SConnection> createTransportConnection(const struct sockaddr* name, int namelen);

        // 's' is consumed.
        boost::shared_ptr<SConnection> createTransportConnection(SYSSOCKET s);

        boost::shared_ptr<SHandle> createStreamSocket(const boost::shared_ptr<SConnection>& conn,
            struct tcp_pcb* pcb);

    private:
        typedef std::set<boost::shared_ptr<LTUDPHandleImpl> > HandleSet;
        typedef std::map<UInt16, boost::weak_ptr<SConnection> > ConnectionCache;

        static err_t netifInitFunc(struct netif* netif);

        static err_t netifInputFunc(struct pbuf* p, struct netif* netif);

        static err_t netifOutputFunc(struct netif* netif, struct pbuf* p, const ip4_addr_t* ipaddr);

        void onRecv(int err, int numBytes, UInt32 srcIp, UInt16 srcPort,
            UInt16 dstPort, const boost::weak_ptr<SConnection>& conn,
            const boost::shared_ptr<std::vector<char> >& rcvBuff);

        void onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);

        void onTcpTimeout();

        void onKillHandles(bool sameThreadOnly);

        void reapConnCache();

        boost::shared_ptr<SConnection> getTransportConnection(UInt16 port);

        // 's' is consumed.
        boost::shared_ptr<SConnection> createTransportConnectionInternal(const struct sockaddr* name, int namelen, SYSSOCKET s);

        SManager& innerMgr_;
        boost::shared_ptr<OpWatch> watch_;
        struct netif netif_;
        std::vector<char> tmpBuff_;

        boost::mutex m_;
        int numAliveHandles_;
        int tcpTimerMod4_;
        ConnectionCache connCache_;
        HandleSet toKillHandles_;
    };
}

#endif
