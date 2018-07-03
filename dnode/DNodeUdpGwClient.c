#include "DNodeUdpGwClient.h"
#include <udpgw/udpgw.h>

static void udpgw_handler_received (DNodeUdpGwClient *o, BAddr local_addr, BAddr remote_addr, const uint8_t *data, int data_len)
{
    DebugObject_Access(&o->d_obj);

    // submit to user
    o->handler_received(o->user, local_addr, remote_addr, data, data_len);
}

int DNodeUdpGwClient_Init (DNodeUdpGwClient *o, int udp_mtu, int max_connections, int send_buffer_size,
                           btime_t keepalive_time, BReactor *reactor, void *user,
                           DNodeUdpGwClient_handler_received handler_received)
{
    // init arguments
    o->udp_mtu = udp_mtu;
    o->reactor = reactor;
    o->user = user;
    o->handler_received = handler_received;

    // init udpgw client
    if (!UdpGwClient_Init(&o->udpgw_client, udp_mtu, max_connections, send_buffer_size, keepalive_time, o->reactor, o,
                          (UdpGwClient_handler_received)udpgw_handler_received
    )) {
        goto fail0;
    }

    UdpGwClient_ConnectServer(&o->udpgw_client, udpgw_get_input(), udpgw_get_output());

    DebugObject_Init(&o->d_obj);
    return 1;

fail0:
    return 0;
}

void DNodeUdpGwClient_Free (DNodeUdpGwClient *o)
{
    UdpGwClient_DisconnectServer(&o->udpgw_client);

    DebugObject_Free(&o->d_obj);

    // free udpgw client
    UdpGwClient_Free(&o->udpgw_client);
}

void DNodeUdpGwClient_SubmitPacket (DNodeUdpGwClient *o, BAddr local_addr, BAddr remote_addr, int is_dns, const uint8_t *data, int data_len)
{
    DebugObject_Access(&o->d_obj);

    UdpGwClient_SubmitPacket(&o->udpgw_client, local_addr, remote_addr, is_dns, data, data_len);
}
