#ifndef DNODE_PROXYTCPCLIENT_H
#define DNODE_PROXYTCPCLIENT_H

#include "DNodeTCPClient.h"

struct DNodeTCPClient* DNodeProxyTCPClient_Create(BAddr dest_addr, DNodeTCPClient_handler handler, void* handler_data, BReactor* reactor);

#endif
