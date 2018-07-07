#ifndef DNODE_TCPCLIENT_H
#define DNODE_TCPCLIENT_H

#include <stdint.h>

#include <misc/debug.h>
#include <misc/debugerror.h>
#include <misc/socks_proto.h>
#include <misc/packed.h>
#include <base/DebugObject.h>
#include <system/BConnection.h>
#include <flow/PacketStreamSender.h>

#define DNODE_TCPCLIENT_EVENT_ERROR 1
#define DNODE_TCPCLIENT_EVENT_UP 2
#define DNODE_TCPCLIENT_EVENT_ERROR_CLOSED 3

typedef void (*DNodeTCPClient_handler) (void* handler_data, int event);

struct DNodeTCPClient
{
    StreamPassInterface* (*get_sender_interface)(struct DNodeTCPClient */*dtcp_client*/);

    StreamRecvInterface* (*get_recv_interface)(struct DNodeTCPClient */*dtcp_client*/);

    void (*destroy)(struct DNodeTCPClient */*dtcp_client*/);

    DNodeTCPClient_handler handler;
    void* handler_data;
};

void DNodeTCPClient_Free(struct DNodeTCPClient* dtcp_client);

StreamPassInterface* DNodeTCPClient_GetSendInterface(struct DNodeTCPClient* dtcp_client);

StreamRecvInterface* DNodeTCPClient_GetRecvInterface(struct DNodeTCPClient* dtcp_client);

#endif
