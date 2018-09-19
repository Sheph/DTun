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

        void addToKill(const boost::shared_ptr<LTUDPHandleImpl>& handle, bool abort);

        boost::shared_ptr<SConnection> createTransportConnection(const struct sockaddr* name, int namelen);

        // 's' is consumed.
        boost::shared_ptr<SConnection> createTransportConnection(SYSSOCKET s);

        boost::shared_ptr<SHandle> createStreamSocket(const boost::shared_ptr<SConnection>& conn,
            struct tcp_pcb* pcb);

        bool haveTransportConnection(UInt16 port) const;

        UInt16 getMappedPeerPort(UInt16 port, UInt32 peerIp, UInt16 peerPort) const;

        uint16_t pcbBindAcceptor(struct tcp_pcb* pcb, uint16_t port);

        uint16_t pcbBindConnector(struct tcp_pcb* pcb, uint16_t port);

        void pcbUnbind(uint16_t pcbPort);

    private:
        typedef std::map<UInt16, UInt16> PortMap;

        struct PeerInfo
        {
            PeerInfo() {}

            PortMap portMap; // send_from -> send_to
        };

        typedef std::map<UInt32, PeerInfo> PeerMap;

        struct ConnectionInfo : boost::noncopyable
        {
            ConnectionInfo() {}
            explicit ConnectionInfo(const boost::shared_ptr<SConnection>& conn)
            : conn(conn)
            , isAcceptor(false)
            , numHandles(0)
            , acceptorLocalPort(0) {}

            boost::weak_ptr<SConnection> conn;
            bool isAcceptor;
            int numHandles;
            PeerMap acceptorPeers;
            uint16_t acceptorLocalPort;
        };

        typedef std::map<boost::shared_ptr<LTUDPHandleImpl>, bool> HandleMap;
        typedef std::map<UInt16, boost::shared_ptr<ConnectionInfo> > ConnectionCache;

        static err_t netifInitFunc(struct netif* netif);

        static err_t netifInputFunc(struct pbuf* p, struct netif* netif);

        static err_t netifOutputFunc(struct netif* netif, struct pbuf* p, const ip4_addr_t* ipaddr);

        void onRecv(int err, int numBytes, UInt32 srcIp, UInt16 srcPort,
            UInt16 dstPort, const boost::shared_ptr<ConnectionInfo>& connInfo,
            const boost::shared_ptr<std::vector<char> >& rcvBuff);

        void onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);

        void onTcpTimeout();

        void onKillHandles(bool sameThreadOnly);

        void onTransportConnectionKill(const boost::shared_ptr<SConnection>& conn);

        void reapConnCache();

        boost::shared_ptr<ConnectionInfo> getConnectionInfo(UInt16 port);

        // 's' is consumed.
        boost::shared_ptr<SConnection> createTransportConnectionInternal(const struct sockaddr* name, int namelen, SYSSOCKET s);

        SManager& innerMgr_;
        boost::shared_ptr<OpWatch> watch_;
        struct netif netif_;
        std::vector<char> tmpBuff_;

        mutable boost::mutex m_;
        int numAliveHandles_;
        int tcpTimerMod4_;
        ConnectionCache connCache_;
        PortMap localPortMap_;
        HandleMap toKillHandles_;
    };
}

#endif
