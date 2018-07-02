#include "DNodeTCPClient.h"

#define STATE_CONNECTING 1
#define STATE_UP 2

static void report_error (DNodeTCPClient *dtcp_client, int error)
{
    DEBUGERROR(&dtcp_client->d_err, dtcp_client->handler(dtcp_client->handler_data, error))
}

static void connection_handler(DNodeTCPClient* dtcp_client, int event)
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

static void init_io(DNodeTCPClient* dtcp_client)
{
    // init receiving
    BConnection_RecvAsync_Init(&dtcp_client->con);

    // init sending
    BConnection_SendAsync_Init(&dtcp_client->con);
}

static void free_io(DNodeTCPClient* dtcp_client)
{
    // free sending
    BConnection_SendAsync_Free(&dtcp_client->con);

    // free receiving
    BConnection_RecvAsync_Free(&dtcp_client->con);
}

static void connector_handler(DNodeTCPClient* dtcp_client, int is_error)
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
    dtcp_client->handler(dtcp_client->handler_data, DNODE_TCPCLIENT_EVENT_UP);

    return;

fail0:
    report_error(dtcp_client, DNODE_TCPCLIENT_EVENT_ERROR);
    return;
}

int DNodeTCPClient_Init(DNodeTCPClient* dtcp_client, BAddr dest_addr, BIPAddr inner_addr, DNodeTCPClient_handler handler, void* handler_data, BReactor* reactor)
{
    ASSERT(dest_addr.type == BADDR_TYPE_IPV4 || dest_addr.type == BADDR_TYPE_IPV6)

    // init arguments
    dtcp_client->dest_addr = dest_addr;
    dtcp_client->handler = handler;
    dtcp_client->handler_data = handler_data;
    dtcp_client->reactor = reactor;

    if (!BConnector_Init2(&dtcp_client->connector, dest_addr, inner_addr, dtcp_client->reactor, dtcp_client, (BConnector_handler)connector_handler)) {
        BLog(BLOG_ERROR, "BConnector_Init failed");
        goto fail0;
    }

    dtcp_client->state = STATE_CONNECTING;

    DebugError_Init(&dtcp_client->d_err, BReactor_PendingGroup(dtcp_client->reactor));
    DebugObject_Init(&dtcp_client->d_obj);
    return 1;

fail0:
    return 0;
}

void DNodeTCPClient_Free(DNodeTCPClient* dtcp_client)
{
    DebugObject_Free(&dtcp_client->d_obj);
    DebugError_Free(&dtcp_client->d_err);

    if (dtcp_client->state != STATE_CONNECTING) {
        free_io(dtcp_client);

        // free connection
        BConnection_Free(&dtcp_client->con);
    }

    // free connector
    BConnector_Free(&dtcp_client->connector);
}

StreamPassInterface* DNodeTCPClient_GetSendInterface(DNodeTCPClient* dtcp_client)
{
    ASSERT(dtcp_client->state == STATE_UP)
    DebugObject_Access(&dtcp_client->d_obj);

    return BConnection_SendAsync_GetIf(&dtcp_client->con);
}

StreamRecvInterface* DNodeTCPClient_GetRecvInterface(DNodeTCPClient* dtcp_client)
{
    ASSERT(dtcp_client->state == STATE_UP)
    DebugObject_Access(&dtcp_client->d_obj);

    return BConnection_RecvAsync_GetIf(&dtcp_client->con);
}
