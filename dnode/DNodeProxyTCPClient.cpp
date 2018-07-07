extern "C" {
#include "DNodeProxyTCPClient.h"
}
#include "DMasterClient.h"
#include "Logger.h"

#define STATE_CONNECTING 1
#define STATE_UP 2

namespace DNode
{
    class ProxyTCPClient : boost::noncopyable
    {
    public:
        explicit ProxyTCPClient(DNodeTCPClient* base)
        : base_(base)
        , state_(STATE_CONNECTING)
        {
        }

        ~ProxyTCPClient()
        {
        }

        bool start(DTun::UInt32 remoteIp, DTun::UInt16 remotePort)
        {
            return false;
        }

    private:
        void onConnect(int err, DTun::UInt32 remoteIp, DTun::UInt16 remotePort)
        {
        }

        DNodeTCPClient* base_;
        int state_;
        SYSSOCKET s_;
    };
}

typedef struct {
    struct DNodeTCPClient base;

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

extern "C" void DNodeProxyTCPClient_Destroy(struct DNodeTCPClient* dtcp_client_)
{
    DNodeProxyTCPClient* dtcp_client = (DNodeProxyTCPClient*)dtcp_client_;

    delete dtcp_client->client;

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

    dtcp_client->client = new DNode::ProxyTCPClient(&dtcp_client->base);

    if (!dtcp_client->client->start(dest_addr.ipv4.ip, dest_addr.ipv4.port)) {
        delete dtcp_client->client;
        free(dtcp_client);
        return NULL;
    }

    return &dtcp_client->base;
}
