#include "DNodeTCPClient.h"

#define STATE_CONNECTING 1
#define STATE_UP 2

typedef struct {
    struct DNodeTCPClient base;
    BAddr dest_addr;
    BReactor* reactor;
    int state;
    BConnector connector;
    BConnection con;
    DebugError d_err;
    DebugObject d_obj;
} DNodeDirectTCPClient;

static void report_error (DNodeDirectTCPClient *dtcp_client, int error)
{
    DEBUGERROR(&dtcp_client->d_err, dtcp_client->base.handler(dtcp_client->base.handler_data, error))
}

static void connection_handler(DNodeDirectTCPClient* dtcp_client, int event)
{
    DebugObject_Access(&dtcp_client->d_obj);
    ASSERT(dtcp_client->state != STATE_CONNECTING)

    if (dtcp_client->state == STATE_UP && event == BCONNECTION_EVENT_RECVCLOSED) {
        report_error(dtcp_client, DNODE_TCPCLIENT_EVENT_ERROR_CLOSED);
        return;
    }

    report_error(dtcp_client, DNODE_TCPCLIENT_EVENT_ERROR);
    return;
}

static void init_io(DNodeDirectTCPClient* dtcp_client)
{
    // init receiving
    BConnection_RecvAsync_Init(&dtcp_client->con);

    // init sending
    BConnection_SendAsync_Init(&dtcp_client->con);
}

static void free_io(DNodeDirectTCPClient* dtcp_client)
{
    // free sending
    BConnection_SendAsync_Free(&dtcp_client->con);

    // free receiving
    BConnection_RecvAsync_Free(&dtcp_client->con);
}

static void connector_handler(DNodeDirectTCPClient* dtcp_client, int is_error)
{
    DebugObject_Access(&dtcp_client->d_obj);
    ASSERT(dtcp_client->state == STATE_CONNECTING)

    // check connection result
    if (is_error) {
        BLog(BLOG_ERROR, "connection failed");
        goto fail0;
    }

    // init connection
    if (!BConnection_Init(&dtcp_client->con, BConnection_source_connector(&dtcp_client->connector), dtcp_client->reactor, dtcp_client, (BConnection_handler)connection_handler)) {
        BLog(BLOG_ERROR, "BConnection_Init failed");
        goto fail0;
    }

    BLog(BLOG_DEBUG, "connected");

    init_io(dtcp_client);

    // set state
    dtcp_client->state = STATE_UP;

    // call handler
    dtcp_client->base.handler(dtcp_client->base.handler_data, DNODE_TCPCLIENT_EVENT_UP);

    return;

fail0:
    report_error(dtcp_client, DNODE_TCPCLIENT_EVENT_ERROR);
    return;
}

static StreamPassInterface* DNodeDirectTCPClient_GetSendInterface(struct DNodeTCPClient* dtcp_client_)
{
    DNodeDirectTCPClient* dtcp_client = (DNodeDirectTCPClient*)dtcp_client_;

    ASSERT(dtcp_client->state == STATE_UP)
    DebugObject_Access(&dtcp_client->d_obj);

    return BConnection_SendAsync_GetIf(&dtcp_client->con);
}

static StreamRecvInterface* DNodeDirectTCPClient_GetRecvInterface(struct DNodeTCPClient* dtcp_client_)
{
    DNodeDirectTCPClient* dtcp_client = (DNodeDirectTCPClient*)dtcp_client_;

    ASSERT(dtcp_client->state == STATE_UP)
    DebugObject_Access(&dtcp_client->d_obj);

    return BConnection_RecvAsync_GetIf(&dtcp_client->con);
}

static void DNodeDirectTCPClient_Destroy(struct DNodeTCPClient* dtcp_client_)
{
    DNodeDirectTCPClient* dtcp_client = (DNodeDirectTCPClient*)dtcp_client_;

    DebugObject_Free(&dtcp_client->d_obj);
    DebugError_Free(&dtcp_client->d_err);

    if (dtcp_client->state != STATE_CONNECTING) {
        free_io(dtcp_client);

        // free connection
        BConnection_Free(&dtcp_client->con);
    }

    // free connector
    BConnector_Free(&dtcp_client->connector);

    free(dtcp_client);
}

struct DNodeTCPClient* DNodeDirectTCPClient_Create(BAddr dest_addr, DNodeTCPClient_handler handler, void* handler_data, BReactor* reactor)
{
    DNodeDirectTCPClient* dtcp_client;

    ASSERT(dest_addr.type == BADDR_TYPE_IPV4 || dest_addr.type == BADDR_TYPE_IPV6)

    if (!(dtcp_client = malloc(sizeof(*dtcp_client)))) {
        BLog(BLOG_ERROR, "malloc failed");
        return NULL;
    }

    // init arguments
    dtcp_client->base.get_sender_interface = &DNodeDirectTCPClient_GetSendInterface;
    dtcp_client->base.get_recv_interface = &DNodeDirectTCPClient_GetRecvInterface;
    dtcp_client->base.destroy = &DNodeDirectTCPClient_Destroy;
    dtcp_client->base.handler = handler;
    dtcp_client->base.handler_data = handler_data;

    dtcp_client->dest_addr = dest_addr;
    dtcp_client->reactor = reactor;

    if (!BConnector_Init(&dtcp_client->connector, dest_addr, dtcp_client->reactor, dtcp_client, (BConnector_handler)connector_handler)) {
        BLog(BLOG_ERROR, "BConnector_Init failed");
        goto fail0;
    }

    dtcp_client->state = STATE_CONNECTING;

    DebugError_Init(&dtcp_client->d_err, BReactor_PendingGroup(dtcp_client->reactor));
    DebugObject_Init(&dtcp_client->d_obj);
    return &dtcp_client->base;

fail0:
    free(dtcp_client);
    return NULL;
}
