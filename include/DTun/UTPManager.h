#ifndef _DTUN_UTPMANAGER_H_
#define _DTUN_UTPMANAGER_H_

#include "DTun/SManager.h"
#include "DTun/OpWatch.h"
#include <boost/thread/mutex.hpp>
#include <boost/array.hpp>
#include "utp.h"
#include <set>

namespace DTun
{
    class UTPHandleImpl;

    class DTUN_API UTPManager : public SManager
    {
    public:
        explicit UTPManager(SManager& mgr);
        ~UTPManager();

        virtual SReactor& reactor();

        bool start();

        virtual boost::shared_ptr<SHandle> createStreamSocket();

        virtual boost::shared_ptr<SHandle> createDatagramSocket(SYSSOCKET s = SYS_INVALID_SOCKET);

        void addToKill(const boost::shared_ptr<UTPHandleImpl>& handle, bool abort);

        boost::shared_ptr<SConnection> createTransportConnection(const struct sockaddr* name, int namelen);

        // 's' is consumed.
        boost::shared_ptr<SConnection> createTransportConnection(SYSSOCKET s);

        UInt16 getMappedPeerPort(UInt16 localPort, UInt32 peerIp, const in_port_utp peerPort) const;

        bool haveTransportConnection(UInt16 port) const;

        inline bool isInRecv() const { return inRecv_; }

        utp_socket* bindAcceptor(UInt16 localPort, UTPHandleImpl* handle);

        utp_socket* bindConnector(UInt16 localPort, UTPHandleImpl* handle, UInt32 ip, UInt16 port);

        void unbind(utp_socket* utpSock);

    private:
        typedef boost::array<uint8_t, 16> UTPPort;

        struct PortInfo
        {
            PortInfo()
            : port(0), active(false) {}

            explicit PortInfo(UInt16 port)
            : port(port), active(false) {}

            UInt16 port;
            bool active;
        };

        typedef std::map<UTPPort, PortInfo> PortMap;

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
            , acceptorHandle(NULL) {}

            ~ConnectionInfo();

            void removeUtpSock(utp_socket* utpSock);

            boost::weak_ptr<SConnection> conn;
            UTPHandleImpl* acceptorHandle;
            PeerMap peers;
            std::set<utp_socket*> utpSocks;
        };

        typedef std::map<boost::shared_ptr<UTPHandleImpl>, bool> HandleMap;
        typedef std::map<UInt16, boost::shared_ptr<ConnectionInfo> > ConnectionCache;

        static uint64 utpLogFunc(utp_callback_arguments* args);

        static uint64 utpSendToFunc(utp_callback_arguments* args);

        static uint64 utpOnErrorFunc(utp_callback_arguments* args);

        static uint64 utpOnStateChangeFunc(utp_callback_arguments* args);

        static uint64 utpOnReadFunc(utp_callback_arguments* args);

        static uint64 utpOnSentFunc(utp_callback_arguments* args);

        static uint64 utpOnFirewallFunc(utp_callback_arguments* args);

        static uint64 utpOnAcceptFunc(utp_callback_arguments* args);

        static uint64 utpGetReadBufferSizeFunc(utp_callback_arguments* args);

        void onRecv(int err, int numBytes, UInt32 srcIp, UInt16 srcPort,
            UInt16 dstPort, const boost::shared_ptr<ConnectionInfo>& connInfo,
            const boost::shared_ptr<std::vector<char> >& rcvBuff);

        void onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);

        void onUTPTimeout();

        void onKillHandles(bool sameThreadOnly);

        void onTransportConnectionKill(const boost::shared_ptr<SConnection>& conn);

        void reapConnCache();

        // 's' is consumed.
        boost::shared_ptr<SConnection> createTransportConnectionInternal(const struct sockaddr* name, int namelen, SYSSOCKET s);

        SManager& innerMgr_;
        boost::shared_ptr<OpWatch> watch_;
        utp_context* ctx_;

        mutable boost::mutex m_;
        int numAliveHandles_;
        ConnectionCache connCache_;
        HandleMap toKillHandles_;
        bool inRecv_;
    };
}

#endif
