#include "DNodeTCPClient.h"

int DNodeTCPClient_Init(DNodeTCPClient* dtcp_client, BAddr dest_addr, DNodeTCPClient_handler handler, void* handler_data, BReactor* reactor)
{
    char dest_addr_s[BADDR_MAX_PRINT_LEN];
    BAddr_Print(&dest_addr, dest_addr_s);
    BLog(BLOG_INFO, "DTCP(%s)", dest_addr_s);
    return 0;
}

void DNodeTCPClient_Free(DNodeTCPClient* dtcp_client)
{
}

StreamPassInterface* DNodeTCPClient_GetSendInterface(DNodeTCPClient* dtcp_client)
{
    return NULL;
}

StreamRecvInterface* DNodeTCPClient_GetRecvInterface(DNodeTCPClient* dtcp_client)
{
    return NULL;
}
