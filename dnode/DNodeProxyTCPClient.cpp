extern "C" {
#include "DNodeProxyTCPClient.h"
#include <system/BThreadSignal.h>
#include <misc/offset.h>
}
#include "DMasterClient.h"
#include "Logger.h"
#include "DTun/Utils.h"
#include <boost/make_shared.hpp>

#define STATE_CONNECTING 1
#define STATE_UP 2
#define STATE_ERR 3

namespace DNode
{
    extern DTun::UDTReactor* theUdtReactor;

    class ProxyTCPClient : boost::noncopyable
    {
    public:
        explicit ProxyTCPClient(BThreadSignal* reactorSignal)
        : reactorSignal_(reactorSignal)
        , state_(STATE_CONNECTING)
        , connId_(0)
        , boundSock_(SYS_INVALID_SOCKET)
        {
        }

        ~ProxyTCPClient()
        {
            DTun::UInt32 connId;

            {
                boost::mutex::scoped_lock lock(m_);
                connId = connId_;
                connId_ = 0;
                reactorSignal_ = NULL;
            }

            if (connId) {
                theMasterClient->cancelConnection(connId);
            }

            if (connector_) {
                connector_->close();
            }

            if (conn_) {
                conn_->close();
            }

            if (boundSock_ != SYS_INVALID_SOCKET) {
                SYS_CLOSE_SOCKET(boundSock_);
                boundSock_ = SYS_INVALID_SOCKET;
            }
        }

        bool start(DTun::UInt32 remoteIp, DTun::UInt16 remotePort)
        {
            SYSSOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (s == SYS_INVALID_SOCKET) {
                LOG4CPLUS_ERROR(logger(), "Cannot create UDP socket");
                return false;
            }

            SYSSOCKET boundSock = dup(s);
            if (boundSock == -1) {
                LOG4CPLUS_ERROR(logger(), "Cannot dup UDP socket");
                SYS_CLOSE_SOCKET(s);
                return false;
            }

            struct sockaddr_in addr;

            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);

            int res = ::bind(s, (const struct sockaddr*)&addr, sizeof(addr));

            if (res == SYS_SOCKET_ERROR) {
                LOG4CPLUS_ERROR(logger(), "Cannot bind UDP socket");
                SYS_CLOSE_SOCKET(s);
                SYS_CLOSE_SOCKET(boundSock);
                return false;
            }

            boost::mutex::scoped_lock lock(m_);

            connId_ = theMasterClient->registerConnection(s, remoteIp, remotePort,
                boost::bind(&ProxyTCPClient::onConnectionRegister, this, _1, _2, _3));
            if (!connId_) {
                SYS_CLOSE_SOCKET(boundSock);
                return false;
            }

            boundSock_ = boundSock;

            return true;
        }

        int getState() const
        {
            boost::mutex::scoped_lock lock(m_);
            return state_;
        }

    private:
        void onConnectionRegister(int err, DTun::UInt32 remoteIp, DTun::UInt16 remotePort)
        {
            LOG4CPLUS_INFO(logger(), "ProxyTCPClient::onConnectionRegister(" << err << ", " << DTun::ipPortToString(remoteIp, remotePort) << ")");

            boost::mutex::scoped_lock lock(m_);
            if (!reactorSignal_) {
                return;
            }

            connId_ = 0;

            if (err) {
                state_ = STATE_ERR;
                signalReactor();
                return;
            }

            UDPSOCKET sock = UDT::socket(AF_INET, SOCK_STREAM, 0);
            if (sock == UDT::INVALID_SOCK) {
                LOG4CPLUS_ERROR(logger(), "Cannot create UDT socket: " << UDT::getlasterror().getErrorMessage());
                state_ = STATE_ERR;
                signalReactor();
                return;
            }

            if (UDT::bind2(sock, boundSock_) == UDT::ERROR) {
                LOG4CPLUS_ERROR(logger(), "Cannot bind UDT socket: " << UDT::getlasterror().getErrorMessage());
                UDT::close(sock);
                state_ = STATE_ERR;
                signalReactor();
                return;
            }

            boundSock_ = SYS_INVALID_SOCKET;

            connector_ = boost::make_shared<DTun::UDTConnector>(boost::ref(*theUdtReactor), sock);

            if (!connector_->connect(DTun::ipToString(remoteIp), DTun::portToString(remotePort),
                boost::bind(&ProxyTCPClient::onConnect, this, _1), true)) {
                state_ = STATE_ERR;
                signalReactor();
                return;
            }
        }

        void onConnect(int err)
        {
            LOG4CPLUS_INFO(logger(), "ProxyTCPClient::onConnect(" << err << ")");

            boost::mutex::scoped_lock lock(m_);
            if (!reactorSignal_) {
                return;
            }

            UDTSOCKET sock = connector_->sock();

            connector_->close();

            UDT::close(sock);
        }

        void signalReactor()
        {
            if (reactorSignal_) {
                while (!BThreadSignal_Thread_Signal(reactorSignal_)) {
                    LOG4CPLUS_ERROR(logger(), "BThreadSignal_Thread_Signal failed");
                    ::usleep(1000);
                }
            }
        }

        mutable boost::mutex m_;
        BThreadSignal* reactorSignal_;
        int state_;
        DTun::UInt32 connId_;
        SYSSOCKET boundSock_;
        boost::shared_ptr<DTun::UDTConnection> conn_;
        boost::shared_ptr<DTun::UDTConnector> connector_;
    };
}

typedef struct {
    struct DNodeTCPClient base;
    BThreadSignal reactorSignal;
    int state;
    DNode::ProxyTCPClient* client;
} DNodeProxyTCPClient;

extern "C" StreamPassInterface* DNodeProxyTCPClient_GetSendInterface(struct DNodeTCPClient* dtcp_client_)
{
    DNodeProxyTCPClient* dtcp_client = (DNodeProxyTCPClient*)dtcp_client_;

    return NULL;
}

extern "C" StreamRecvInterface* DNodeProxyTCPClient_GetRecvInterface(struct DNodeTCPClient* dtcp_client_)
{
    DNodeProxyTCPClient* dtcp_client = (DNodeProxyTCPClient*)dtcp_client_;

    return NULL;
}

extern "C" void DNodeProxyTCPClient_SignalHandler(BThreadSignal* reactorSignal)
{
    DNodeProxyTCPClient* dtcp_client = UPPER_OBJECT(reactorSignal, DNodeProxyTCPClient, reactorSignal);

    LOG4CPLUS_TRACE(DNode::logger(), "wakeup");

    int new_state = dtcp_client->client->getState();

    if (dtcp_client->state != new_state) {
        dtcp_client->state = new_state;
        if (dtcp_client->state == STATE_UP) {
            dtcp_client->base.handler(dtcp_client->base.handler_data, DNODE_TCPCLIENT_EVENT_UP);
        } else if (dtcp_client->state == STATE_ERR) {
            dtcp_client->base.handler(dtcp_client->base.handler_data, DNODE_TCPCLIENT_EVENT_ERROR);
        }
    }
}

extern "C" void DNodeProxyTCPClient_Destroy(struct DNodeTCPClient* dtcp_client_)
{
    DNodeProxyTCPClient* dtcp_client = (DNodeProxyTCPClient*)dtcp_client_;

    delete dtcp_client->client;

    BThreadSignal_Free(&dtcp_client->reactorSignal);

    free(dtcp_client);
}

extern "C" struct DNodeTCPClient* DNodeProxyTCPClient_Create(BAddr dest_addr, DNodeTCPClient_handler handler, void* handler_data, BReactor* reactor)
{
    DNodeProxyTCPClient* dtcp_client;

    ASSERT(dest_addr.type == BADDR_TYPE_IPV4 || dest_addr.type == BADDR_TYPE_IPV6)

    if (!(dtcp_client = (DNodeProxyTCPClient*)malloc(sizeof(*dtcp_client)))) {
        LOG4CPLUS_ERROR(DNode::logger(), "malloc failed");
        return NULL;
    }

    // init arguments
    dtcp_client->base.get_sender_interface = &DNodeProxyTCPClient_GetSendInterface;
    dtcp_client->base.get_recv_interface = &DNodeProxyTCPClient_GetRecvInterface;
    dtcp_client->base.destroy = &DNodeProxyTCPClient_Destroy;
    dtcp_client->base.handler = handler;
    dtcp_client->base.handler_data = handler_data;

    if (!BThreadSignal_Init(&dtcp_client->reactorSignal, reactor, DNodeProxyTCPClient_SignalHandler)) {
        LOG4CPLUS_ERROR(DNode::logger(), "BThreadSignal_Init");
        free(dtcp_client);
        return NULL;
    }

    dtcp_client->state = STATE_CONNECTING;

    dtcp_client->client = new DNode::ProxyTCPClient(&dtcp_client->reactorSignal);

    if (!dtcp_client->client->start(dest_addr.ipv4.ip, dest_addr.ipv4.port)) {
        delete dtcp_client->client;
        BThreadSignal_Free(&dtcp_client->reactorSignal);
        free(dtcp_client);
        return NULL;
    }

    return &dtcp_client->base;
}
