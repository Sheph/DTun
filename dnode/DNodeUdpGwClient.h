#ifndef DNODE_UDPGWCLIENT_H
#define DNODE_UDPGWCLIENT_H

#include <misc/debug.h>
#include <base/DebugObject.h>
#include <system/BReactor.h>
#include <udpgw_client/UdpGwClient.h>

typedef void (*DNodeUdpGwClient_handler_received) (void *user, BAddr local_addr, BAddr remote_addr, const uint8_t *data, int data_len);

typedef struct {
    int udp_mtu;
    BReactor *reactor;
    void *user;
    DNodeUdpGwClient_handler_received handler_received;
    UdpGwClient udpgw_client;
    DebugObject d_obj;
} DNodeUdpGwClient;

int DNodeUdpGwClient_Init (DNodeUdpGwClient *o, int udp_mtu, int max_connections, int send_buffer_size,
                           btime_t keepalive_time, BReactor *reactor, void *user,
                           DNodeUdpGwClient_handler_received handler_received) WARN_UNUSED;
void DNodeUdpGwClient_Free (DNodeUdpGwClient *o);
void DNodeUdpGwClient_SubmitPacket (DNodeUdpGwClient *o, BAddr local_addr, BAddr remote_addr, int is_dns, const uint8_t *data, int data_len);

#endif
