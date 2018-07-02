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

typedef struct {
    DNodeTCPClient_handler handler;
    void* handler_data;
    BReactor* reactor;
    BConnector connector;
    BConnection con;
} DNodeTCPClient;

int DNodeTCPClient_Init(DNodeTCPClient* dtcp_client, BAddr dest_addr, DNodeTCPClient_handler handler, void* handler_data, BReactor* reactor);

void DNodeTCPClient_Free(DNodeTCPClient* dtcp_client);

StreamPassInterface* DNodeTCPClient_GetSendInterface(DNodeTCPClient* dtcp_client);

StreamRecvInterface* DNodeTCPClient_GetRecvInterface(DNodeTCPClient* dtcp_client);

#endif
