#include "DNodeTCPClient.h"

void DNodeTCPClient_Free(struct DNodeTCPClient* dtcp_client)
{
    dtcp_client->destroy(dtcp_client);
}

StreamPassInterface* DNodeTCPClient_GetSendInterface(struct DNodeTCPClient* dtcp_client)
{
    return dtcp_client->get_sender_interface(dtcp_client);
}

StreamRecvInterface* DNodeTCPClient_GetRecvInterface(struct DNodeTCPClient* dtcp_client)
{
    return dtcp_client->get_recv_interface(dtcp_client);
}
