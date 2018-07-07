#ifndef DNODE_DIRECTTCPCLIENT_H
#define DNODE_DIRECTTCPCLIENT_H

#include "DNodeTCPClient.h"
#include <system/BReactor.h>
#include <system/BAddr.h>

struct DNodeTCPClient* DNodeDirectTCPClient_Create(BAddr dest_addr, DNodeTCPClient_handler handler, void* handler_data, BReactor* reactor);

#endif
