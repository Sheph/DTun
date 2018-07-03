/*
 * Copyright (C) Ambroz Bizjak <ambrop7@gmail.com>
 * Contributions:
 * Transparent DNS: Copyright (C) Kerem Hadimli <kerem.hadimli@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the
 *    names of its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>

#include <protocol/udpgw_proto.h>
#include <misc/debug.h>
#include <misc/version.h>
#include <misc/loggers_string.h>
#include <misc/loglevel.h>
#include <misc/offset.h>
#include <misc/byteorder.h>
#include <misc/bsize.h>
#include <misc/open_standard_streams.h>
#include <misc/balloc.h>
#include <misc/compare.h>
#include <misc/print_macros.h>
#include <structure/LinkedList1.h>
#include <structure/BAVL.h>
#include <base/BLog.h>
#include <system/BNetwork.h>
#include <system/BConnection.h>
#include <system/BDatagram.h>
#include <system/BSignal.h>
#include <flow/PacketProtoDecoder.h>
#include <flow/PacketPassFairQueue.h>
#include <flow/PacketStreamSender.h>
#include <flow/PacketProtoFlowPassthru.h>
#include <flow/SinglePacketBuffer.h>

#ifndef BADVPN_USE_WINAPI
#include <base/BLog_syslog.h>
#include <arpa/nameser.h>
#include <resolv.h>
#endif

#include <udpgw/udpgw.h>

#define DNS_UPDATE_TIME 2000

struct client {
    PacketPassConnector send_connector;
    PacketPassConnector recv_connector;
    PacketPassInterface recv_if;
    PacketPassFairQueue send_queue;
    BAVL connections_tree;
    LinkedList1 connections_list;
    int num_connections;
    LinkedList1 closing_connections_list;
    LinkedList1Node clients_list_node;
};

struct connection {
    struct client *client;
    uint16_t conid;
    BAddr addr;
    BAddr orig_addr;
    const uint8_t *first_data;
    int first_data_len;
    btime_t last_use_time;
    int closing;
    BPending first_job;
    BufferWriter *send_if;
    PacketProtoFlowPassthru send_ppflow;
    PacketPassFairQueueFlow send_qflow;
    union {
        struct {
            BDatagram udp_dgram;
            int local_port_index;
            BufferWriter udp_send_writer;
            PacketBuffer udp_send_buffer;
            SinglePacketBuffer udp_recv_buffer;
            PacketPassInterface udp_recv_if;
            BAVLNode connections_tree_node;
            LinkedList1Node connections_list_node;
        };
        struct {
            LinkedList1Node closing_connections_list_node;
        };
    };
};

static struct {
    int max_connections_for_client;
    int local_udp_num_ports;
    char *local_udp_addr;
    int local_udp_ip6_num_ports;
    char *local_udp_ip6_addr;
    int unique_local_ports;
} options;

// MTUs
static int udp_mtu;
static int udpgw_mtu;

// local UDP port range, if options.local_udp_num_ports>=0
static BAddr local_udp_addr;

// local UDP/IPv6 port range, if options.local_udp_ip6_num_ports>=0
static BAddr local_udp_ip6_addr;

// DNS forwarding
static BAddr dns_addr;
static btime_t last_dns_update_time;

// reactor
static BReactor* ss;

// the one and only client
static struct client the_client;

static void parse_arguments (int argc, char *argv[]);
static void process_arguments (void);
static void client_init (struct client *client);
static void client_free (struct client *client);
static void client_logfunc (struct client *client);
static void client_log (struct client *client, int level, const char *fmt, ...);
static void client_recv_if_handler_send (struct client *client, uint8_t *data, int data_len);
static int get_local_num_ports (int addr_type);
static BAddr get_local_addr (int addr_type);
static uint8_t * build_port_usage_array_and_find_least_used_connection (BAddr remote_addr, struct connection **out_con);
static void connection_init (struct client *client, uint16_t conid, BAddr addr, BAddr orig_addr, const uint8_t *data, int data_len);
static void connection_free (struct connection *con);
static void connection_logfunc (struct connection *con);
static void connection_log (struct connection *con, int level, const char *fmt, ...);
static void connection_free_udp (struct connection *con);
static void connection_first_job_handler (struct connection *con);
static void connection_send_to_client (struct connection *con, uint8_t flags, const uint8_t *data, int data_len);
static int connection_send_to_udp (struct connection *con, const uint8_t *data, int data_len);
static void connection_close (struct connection *con);
static void connection_send_qflow_busy_handler (struct connection *con);
static void connection_dgram_handler_event (struct connection *con, int event);
static void connection_udp_recv_if_handler_send (struct connection *con, uint8_t *data, int data_len);
static struct connection * find_connection (struct client *client, uint16_t conid);
static int uint16_comparator (void *unused, uint16_t *v1, uint16_t *v2);
static void maybe_update_dns (void);

void udpgw_init (int argc, char **argv, BReactor* udpgw_reactor, int udp_mtu_)
{
    // parse command-line arguments
    parse_arguments(argc, argv);

    // process arguments
    process_arguments();

    ss = udpgw_reactor;

    // compute MTUs
    udp_mtu = udp_mtu_;
    udpgw_mtu = udpgw_compute_mtu(udp_mtu);
    if (udpgw_mtu < 0 || udpgw_mtu > PACKETPROTO_MAXPAYLOAD) {
        udpgw_mtu = PACKETPROTO_MAXPAYLOAD;
    }

    // init DNS forwarding
    BAddr_InitNone(&dns_addr);
    last_dns_update_time = INT64_MIN;
    maybe_update_dns();

    client_init(&the_client);
}

PacketPassInterface* udpgw_get_input ()
{
    return PacketPassConnector_GetInput(&the_client.recv_connector);
}

PacketPassConnector* udpgw_get_output ()
{
    return &the_client.send_connector;
}

void udpgw_cleanup ()
{
    client_free(&the_client);
}

void parse_arguments (int argc, char *argv[])
{
    if (argc <= 0) {
        return;
    }

    options.max_connections_for_client = DEFAULT_MAX_CONNECTIONS_FOR_CLIENT;
    options.local_udp_num_ports = -1;
    options.local_udp_ip6_num_ports = -1;
    options.unique_local_ports = 0;

    int i;
    for (i = 1; i < argc; i++) {
        char *arg = argv[i];

        if (!strcmp(arg, "--max-connections-for-client")) {
            if (1 >= argc - i) {
                fprintf(stderr, "%s: requires an argument\n", arg);
                return;
            }
            if ((options.max_connections_for_client = atoi(argv[i + 1])) <= 0) {
                fprintf(stderr, "%s: wrong argument\n", arg);
                return;
            }
            i++;
        }
        else if (!strcmp(arg, "--local-udp-addrs")) {
            if (2 >= argc - i) {
                fprintf(stderr, "%s: requires two arguments\n", arg);
                return;
            }
            options.local_udp_addr = argv[i + 1];
            if ((options.local_udp_num_ports = atoi(argv[i + 2])) < 0) {
                fprintf(stderr, "%s: wrong argument\n", arg);
                return;
            }
            i += 2;
        }
        else if (!strcmp(arg, "--local-udp-ip6-addrs")) {
            if (2 >= argc - i) {
                fprintf(stderr, "%s: requires two arguments\n", arg);
                return;
            }
            options.local_udp_ip6_addr = argv[i + 1];
            if ((options.local_udp_ip6_num_ports = atoi(argv[i + 2])) < 0) {
                fprintf(stderr, "%s: wrong argument\n", arg);
                return;
            }
            i += 2;
        }
        else if (!strcmp(arg, "--unique-local-ports")) {
            options.unique_local_ports = 1;
        }
    }
}

void process_arguments (void)
{
    // resolve local UDP address
    if (options.local_udp_num_ports >= 0) {
        if (!BAddr_Parse(&local_udp_addr, options.local_udp_addr, NULL, 0)) {
            BLog(BLOG_ERROR, "local udp addr: BAddr_Parse failed");
            return;
        }
        if (local_udp_addr.type != BADDR_TYPE_IPV4) {
            BLog(BLOG_ERROR, "local udp addr: must be an IPv4 address");
            return;
        }
    }

    // resolve local UDP/IPv6 address
    if (options.local_udp_ip6_num_ports >= 0) {
        if (!BAddr_Parse(&local_udp_ip6_addr, options.local_udp_ip6_addr, NULL, 0)) {
            BLog(BLOG_ERROR, "local udp ip6 addr: BAddr_Parse failed");
            return;
        }
        if (local_udp_ip6_addr.type != BADDR_TYPE_IPV6) {
            BLog(BLOG_ERROR, "local udp ip6 addr: must be an IPv6 address");
            return;
        }
    }
}

void client_init (struct client *client)
{
    memset(client, 0, sizeof(*client));

    PacketPassConnector_Init(&client->send_connector, udpgw_mtu, BReactor_PendingGroup(ss));
    PacketPassConnector_Init(&client->recv_connector, udpgw_mtu, BReactor_PendingGroup(ss));

    // init recv interface
    PacketPassInterface_Init(&client->recv_if, udpgw_mtu, (PacketPassInterface_handler_send)client_recv_if_handler_send, client, BReactor_PendingGroup(ss));
    PacketPassConnector_ConnectOutput(&client->recv_connector, &client->recv_if);

    // init send queue
    if (!PacketPassFairQueue_Init(&client->send_queue, PacketPassConnector_GetInput(&client->send_connector), BReactor_PendingGroup(ss), 0, 1)) {
        BLog(BLOG_ERROR, "PacketPassFairQueue_Init failed");
    }

    // init connections tree
    BAVL_Init(&client->connections_tree, OFFSET_DIFF(struct connection, conid, connections_tree_node), (BAVL_comparator)uint16_comparator, NULL);

    // init connections list
    LinkedList1_Init(&client->connections_list);

    // set zero connections
    client->num_connections = 0;

    // init closing connections list
    LinkedList1_Init(&client->closing_connections_list);
}

void client_free (struct client *client)
{
    // allow freeing send queue flows
    PacketPassFairQueue_PrepareFree(&client->send_queue);

    // free connections
    while (!LinkedList1_IsEmpty(&client->connections_list)) {
        struct connection *con = UPPER_OBJECT(LinkedList1_GetFirst(&client->connections_list), struct connection, connections_list_node);
        connection_free(con);
    }

    // free closing connections
    while (!LinkedList1_IsEmpty(&client->closing_connections_list)) {
        struct connection *con = UPPER_OBJECT(LinkedList1_GetFirst(&client->closing_connections_list), struct connection, closing_connections_list_node);
        connection_free(con);
    }

    // free send queue
    PacketPassFairQueue_Free(&client->send_queue);

    PacketPassConnector_Free(&client->send_connector);
    PacketPassConnector_Free(&client->recv_connector);
    PacketPassInterface_Free(&client->recv_if);
}

void client_logfunc (struct client *client)
{
    BLog_Append("udpgw client: ");
}

void client_log (struct client *client, int level, const char *fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    BLog_LogViaFuncVarArg((BLog_logfunc)client_logfunc, client, BLOG_CURRENT_CHANNEL, level, fmt, vl);
    va_end(vl);
}

void client_recv_if_handler_send (struct client *client, uint8_t *data, int data_len)
{
    ASSERT(data_len >= 0)
    ASSERT(data_len <= udpgw_mtu)

    // accept packet
    PacketPassInterface_Done(&client->recv_if);

    // parse header
    if (data_len < sizeof(struct udpgw_header)) {
        client_log(client, BLOG_ERROR, "missing header");
        return;
    }
    struct udpgw_header header;
    memcpy(&header, data, sizeof(header));
    data += sizeof(header);
    data_len -= sizeof(header);
    uint8_t flags = ltoh8(header.flags);
    uint16_t conid = ltoh16(header.conid);

    // if this is keepalive, ignore any payload
    if ((flags & UDPGW_CLIENT_FLAG_KEEPALIVE)) {
        client_log(client, BLOG_DEBUG, "received keepalive");
        return;
    }

    // parse address
    BAddr orig_addr;
    if ((flags & UDPGW_CLIENT_FLAG_IPV6)) {
        if (data_len < sizeof(struct udpgw_addr_ipv6)) {
            client_log(client, BLOG_ERROR, "missing ipv6 address");
            return;
        }
        struct udpgw_addr_ipv6 addr_ipv6;
        memcpy(&addr_ipv6, data, sizeof(addr_ipv6));
        data += sizeof(addr_ipv6);
        data_len -= sizeof(addr_ipv6);
        BAddr_InitIPv6(&orig_addr, addr_ipv6.addr_ip, addr_ipv6.addr_port);
    } else {
        if (data_len < sizeof(struct udpgw_addr_ipv4)) {
            client_log(client, BLOG_ERROR, "missing ipv4 address");
            return;
        }
        struct udpgw_addr_ipv4 addr_ipv4;
        memcpy(&addr_ipv4, data, sizeof(addr_ipv4));
        data += sizeof(addr_ipv4);
        data_len -= sizeof(addr_ipv4);
        BAddr_InitIPv4(&orig_addr, addr_ipv4.addr_ip, addr_ipv4.addr_port);
    }

    // check payload length
    if (data_len > udp_mtu) {
        client_log(client, BLOG_ERROR, "too much data");
        return;
    }

    // find connection
    struct connection *con = find_connection(client, conid);
    ASSERT(!con || !con->closing)

    // if connection exists, close it if needed
    if (con && ((flags & UDPGW_CLIENT_FLAG_REBIND) || !BAddr_Compare(&con->orig_addr, &orig_addr))) {
        connection_log(con, BLOG_DEBUG, "close old");
        connection_close(con);
        con = NULL;
    }

    // if connection doesn't exists, create it
    if (!con) {
        // check number of connections
        if (client->num_connections == options.max_connections_for_client) {
            // close least recently used connection
            con = UPPER_OBJECT(LinkedList1_GetFirst(&client->connections_list), struct connection, connections_list_node);
            connection_close(con);
        }

        // if this is DNS, replace actual address, but keep still remember the orig_addr
        BAddr addr = orig_addr;
        if ((flags & UDPGW_CLIENT_FLAG_DNS)) {
            maybe_update_dns();
            if (dns_addr.type == BADDR_TYPE_NONE) {
                client_log(client, BLOG_WARNING, "received DNS packet, but no DNS server available");
            } else {
                client_log(client, BLOG_DEBUG, "received DNS");
                addr = dns_addr;
            }
        }

        // create new connection
        connection_init(client, conid, addr, orig_addr, data, data_len);
    } else {
        // submit packet to existing connection
        connection_send_to_udp(con, data, data_len);
    }
}

int get_local_num_ports (int addr_type)
{
    switch (addr_type) {
        case BADDR_TYPE_IPV4: return options.local_udp_num_ports;
        case BADDR_TYPE_IPV6: return options.local_udp_ip6_num_ports;
        default: ASSERT(0); return 0;
    }
}

BAddr get_local_addr (int addr_type)
{
    ASSERT(get_local_num_ports(addr_type) >= 0)

    switch (addr_type) {
        case BADDR_TYPE_IPV4: return local_udp_addr;
        case BADDR_TYPE_IPV6: return local_udp_ip6_addr;
        default: ASSERT(0); return BAddr_MakeNone();
    }
}

uint8_t * build_port_usage_array_and_find_least_used_connection (BAddr remote_addr, struct connection **out_con)
{
    ASSERT(remote_addr.type == BADDR_TYPE_IPV4 || remote_addr.type == BADDR_TYPE_IPV6)
    ASSERT(get_local_num_ports(remote_addr.type) >= 0)

    int local_num_ports = get_local_num_ports(remote_addr.type);

    // allocate port usage array
    uint8_t *port_usage = (uint8_t *)BAllocSize(bsize_fromint(local_num_ports));
    if (!port_usage) {
        return NULL;
    }

    // zero array
    memset(port_usage, 0, local_num_ports);

    struct connection *least_con = NULL;

    // flag inappropriate ports (those with the same remote address)
    {
        struct client *client = &the_client;

        for (LinkedList1Node *ln2 = LinkedList1_GetFirst(&client->connections_list); ln2; ln2 = LinkedList1Node_Next(ln2)) {
            struct connection *con = UPPER_OBJECT(ln2, struct connection, connections_list_node);
            ASSERT(con->client == client)
            ASSERT(!con->closing)

            if (con->addr.type != remote_addr.type || con->local_port_index < 0) {
                continue;
            }
            ASSERT(con->local_port_index < local_num_ports)

            if (options.unique_local_ports) {
                BIPAddr ip1;
                BIPAddr ip2;
                BAddr_GetIPAddr(&con->addr, &ip1);
                BAddr_GetIPAddr(&remote_addr, &ip2);
                if (!BIPAddr_Compare(&ip1, &ip2)) {
                    continue;
                }
            } else {
                if (!BAddr_Compare(&con->addr, &remote_addr)) {
                    continue;
                }
            }

            port_usage[con->local_port_index] = 1;

            if (!PacketPassFairQueueFlow_IsBusy(&con->send_qflow)) {
                if (!least_con || con->last_use_time < least_con->last_use_time) {
                    least_con = con;
                }
            }
        }
    }

    *out_con = least_con;
    return port_usage;
}

void connection_init (struct client *client, uint16_t conid, BAddr addr, BAddr orig_addr, const uint8_t *data, int data_len)
{
    ASSERT(client->num_connections < options.max_connections_for_client)
    ASSERT(!find_connection(client, conid))
    BAddr_Assert(&addr);
    ASSERT(addr.type == BADDR_TYPE_IPV4 || addr.type == BADDR_TYPE_IPV6)
    ASSERT(orig_addr.type == BADDR_TYPE_IPV4 || orig_addr.type == BADDR_TYPE_IPV6)
    ASSERT(data_len >= 0)
    ASSERT(data_len <= udp_mtu)

    // allocate structure
    struct connection *con = (struct connection *)malloc(sizeof(*con));
    if (!con) {
        client_log(client, BLOG_ERROR, "malloc failed");
        goto fail0;
    }

    // init arguments
    con->client = client;
    con->conid = conid;
    con->addr = addr;
    con->orig_addr = orig_addr;
    con->first_data = data;
    con->first_data_len = data_len;

    // set last use time
    con->last_use_time = btime_gettime();

    // set not closing
    con->closing = 0;

    // init first job
    BPending_Init(&con->first_job, BReactor_PendingGroup(ss), (BPending_handler)connection_first_job_handler, con);
    BPending_Set(&con->first_job);

    // init send queue flow
    PacketPassFairQueueFlow_Init(&con->send_qflow, &client->send_queue);

    // init send PacketProtoFlow
    if (!PacketProtoFlowPassthru_Init(&con->send_ppflow, udpgw_mtu, CONNECTION_CLIENT_BUFFER_SIZE, PacketPassFairQueueFlow_GetInput(&con->send_qflow), BReactor_PendingGroup(ss))) {
        client_log(client, BLOG_ERROR, "PacketProtoFlow_Init failed");
        goto fail1;
    }
    con->send_if = PacketProtoFlowPassthru_GetInput(&con->send_ppflow);

    // init UDP dgram
    if (!BDatagram_Init(&con->udp_dgram, addr.type, ss, con, (BDatagram_handler)connection_dgram_handler_event)) {
        client_log(client, BLOG_ERROR, "BDatagram_Init failed");
        goto fail2;
    }

    con->local_port_index = -1;

    int local_num_ports = get_local_num_ports(addr.type);

    if (local_num_ports >= 0) {
        // build port usage array, find least used connection
        struct connection *least_con;
        uint8_t *port_usage = build_port_usage_array_and_find_least_used_connection(addr, &least_con);
        if (!port_usage) {
            client_log(client, BLOG_ERROR, "build_port_usage_array failed");
            goto failed;
        }

        // set SO_REUSEADDR
        if (!BDatagram_SetReuseAddr(&con->udp_dgram, 1)) {
            client_log(client, BLOG_ERROR, "set SO_REUSEADDR failed");
            goto failed;
        }

        // get starting local address
        BAddr local_addr = get_local_addr(addr.type);

        // try different ports
        for (int i = 0; i < local_num_ports; i++) {
            // skip inappropriate ports
            if (port_usage[i]) {
                continue;
            }

            BAddr bind_addr = local_addr;
            BAddr_SetPort(&bind_addr, hton16(ntoh16(BAddr_GetPort(&bind_addr)) + (uint16_t)i));
            if (BDatagram_Bind(&con->udp_dgram, bind_addr)) {
                // remember which port we're using
                con->local_port_index = i;
                goto cont;
            }
        }

        // try closing an unused connection with the same remote addr
        if (!least_con) {
            goto failed;
        }

        ASSERT(least_con->addr.type == addr.type)
        ASSERT(least_con->local_port_index >= 0)
        ASSERT(least_con->local_port_index < local_num_ports)
        ASSERT(!PacketPassFairQueueFlow_IsBusy(&least_con->send_qflow))

        int i = least_con->local_port_index;

        BLog(BLOG_INFO, "closing connection for its remote address");

        // close the offending connection
        connection_close(least_con);

        // try binding to its port
        BAddr bind_addr = local_addr;
        BAddr_SetPort(&bind_addr, hton16(ntoh16(BAddr_GetPort(&bind_addr)) + (uint16_t)i));
        if (BDatagram_Bind(&con->udp_dgram, bind_addr)) {
            // remember which port we're using
            con->local_port_index = i;
            goto cont;
        }

    failed:
        client_log(client, BLOG_WARNING, "failed to bind to any local address; proceeding regardless");
    cont:;
        BFree(port_usage);
    }

    // set UDP dgram send address
    BIPAddr ipaddr;
    BIPAddr_InitInvalid(&ipaddr);
    BDatagram_SetSendAddrs(&con->udp_dgram, addr, ipaddr);

    // init UDP dgram interfaces
    BDatagram_SendAsync_Init(&con->udp_dgram, udp_mtu);
    BDatagram_RecvAsync_Init(&con->udp_dgram, udp_mtu);

    // init UDP writer
    BufferWriter_Init(&con->udp_send_writer, udp_mtu, BReactor_PendingGroup(ss));

    // init UDP buffer
    if (!PacketBuffer_Init(&con->udp_send_buffer, BufferWriter_GetOutput(&con->udp_send_writer), BDatagram_SendAsync_GetIf(&con->udp_dgram), CONNECTION_UDP_BUFFER_SIZE, BReactor_PendingGroup(ss))) {
        client_log(client, BLOG_ERROR, "PacketBuffer_Init failed");
        goto fail4;
    }

    // init UDP recv interface
    PacketPassInterface_Init(&con->udp_recv_if, udp_mtu, (PacketPassInterface_handler_send)connection_udp_recv_if_handler_send, con, BReactor_PendingGroup(ss));

    // init UDP recv buffer
    if (!SinglePacketBuffer_Init(&con->udp_recv_buffer, BDatagram_RecvAsync_GetIf(&con->udp_dgram), &con->udp_recv_if, BReactor_PendingGroup(ss))) {
        client_log(client, BLOG_ERROR, "SinglePacketBuffer_Init failed");
        goto fail5;
    }

    // insert to client's connections tree
    ASSERT_EXECUTE(BAVL_Insert(&client->connections_tree, &con->connections_tree_node, NULL))

    // insert to client's connections list
    LinkedList1_Append(&client->connections_list, &con->connections_list_node);

    // increment number of connections
    client->num_connections++;

    connection_log(con, BLOG_DEBUG, "initialized");

    return;

fail5:
    PacketPassInterface_Free(&con->udp_recv_if);
    PacketBuffer_Free(&con->udp_send_buffer);
fail4:
    BufferWriter_Free(&con->udp_send_writer);
    BDatagram_RecvAsync_Free(&con->udp_dgram);
    BDatagram_SendAsync_Free(&con->udp_dgram);
    BDatagram_Free(&con->udp_dgram);
fail2:
    PacketProtoFlowPassthru_Free(&con->send_ppflow);
fail1:
    PacketPassFairQueueFlow_Free(&con->send_qflow);
    BPending_Free(&con->first_job);
    free(con);
fail0:
    return;
}

void connection_free (struct connection *con)
{
    struct client *client = con->client;
    PacketPassFairQueueFlow_AssertFree(&con->send_qflow);

    if (con->closing) {
        // remove from client's closing connections list
        LinkedList1_Remove(&client->closing_connections_list, &con->closing_connections_list_node);
    } else {
        // decrement number of connections
        client->num_connections--;

        // remove from client's connections list
        LinkedList1_Remove(&client->connections_list, &con->connections_list_node);

        // remove from client's connections tree
        BAVL_Remove(&client->connections_tree, &con->connections_tree_node);

        // free UDP
        connection_free_udp(con);
    }

    // free send PacketProtoFlow
    PacketProtoFlowPassthru_Free(&con->send_ppflow);

    // free send queue flow
    PacketPassFairQueueFlow_Free(&con->send_qflow);

    // free first job
    BPending_Free(&con->first_job);

    // free structure
    free(con);
}

void connection_logfunc (struct connection *con)
{
    client_logfunc(con->client);

    if (con->closing) {
        BLog_Append("old connection %"PRIu16": ", con->conid);
    } else {
        BLog_Append("connection %"PRIu16": ", con->conid);
    }
}

void connection_log (struct connection *con, int level, const char *fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    BLog_LogViaFuncVarArg((BLog_logfunc)connection_logfunc, con, BLOG_CURRENT_CHANNEL, level, fmt, vl);
    va_end(vl);
}

void connection_free_udp (struct connection *con)
{
    // free UDP receive buffer
    SinglePacketBuffer_Free(&con->udp_recv_buffer);

    // free UDP receive interface
    PacketPassInterface_Free(&con->udp_recv_if);

    // free UDP buffer
    PacketBuffer_Free(&con->udp_send_buffer);

    // free UDP writer
    BufferWriter_Free(&con->udp_send_writer);

    // free UDP dgram interfaces
    BDatagram_RecvAsync_Free(&con->udp_dgram);
    BDatagram_SendAsync_Free(&con->udp_dgram);

    // free UDP dgram
    BDatagram_Free(&con->udp_dgram);
}

void connection_first_job_handler (struct connection *con)
{
    ASSERT(!con->closing)

    connection_send_to_udp(con, con->first_data, con->first_data_len);
}

void connection_send_to_client (struct connection *con, uint8_t flags, const uint8_t *data, int data_len)
{
    ASSERT(data_len >= 0)
    ASSERT(data_len <= udp_mtu)

    size_t addr_len = (con->orig_addr.type == BADDR_TYPE_IPV6) ? sizeof(struct udpgw_addr_ipv6) :
                      (con->orig_addr.type == BADDR_TYPE_IPV4) ? sizeof(struct udpgw_addr_ipv4) : 0;
    if (data_len > udpgw_mtu - (int)(sizeof(struct udpgw_header) + addr_len)) {
        connection_log(con, BLOG_WARNING, "packet is too large, cannot send to client");
        return;
    }

    // get buffer location
    uint8_t *out;
    if (!BufferWriter_StartPacket(con->send_if, &out)) {
        connection_log(con, BLOG_ERROR, "out of client buffer");
        return;
    }
    int out_pos = 0;

    if (con->orig_addr.type == BADDR_TYPE_IPV6) {
        flags |= UDPGW_CLIENT_FLAG_IPV6;
    }

    // write header
    struct udpgw_header header;
    header.flags = htol8(flags);
    header.conid = htol16(con->conid);
    memcpy(out + out_pos, &header, sizeof(header));
    out_pos += sizeof(header);

    // write address
    switch (con->orig_addr.type) {
        case BADDR_TYPE_IPV4: {
            struct udpgw_addr_ipv4 addr_ipv4;
            addr_ipv4.addr_ip = con->orig_addr.ipv4.ip;
            addr_ipv4.addr_port = con->orig_addr.ipv4.port;
            memcpy(out + out_pos, &addr_ipv4, sizeof(addr_ipv4));
            out_pos += sizeof(addr_ipv4);
        } break;
        case BADDR_TYPE_IPV6: {
            struct udpgw_addr_ipv6 addr_ipv6;
            memcpy(addr_ipv6.addr_ip, con->orig_addr.ipv6.ip, sizeof(addr_ipv6.addr_ip));
            addr_ipv6.addr_port = con->orig_addr.ipv6.port;
            memcpy(out + out_pos, &addr_ipv6, sizeof(addr_ipv6));
            out_pos += sizeof(addr_ipv6);
        } break;
    }

    // write message
    memcpy(out + out_pos, data, data_len);
    out_pos += data_len;

    // submit written message
    ASSERT(out_pos <= udpgw_mtu)
    BufferWriter_EndPacket(con->send_if, out_pos);
}

int connection_send_to_udp (struct connection *con, const uint8_t *data, int data_len)
{
    struct client *client = con->client;
    ASSERT(!con->closing)
    ASSERT(data_len >= 0)
    ASSERT(data_len <= udp_mtu)

    connection_log(con, BLOG_DEBUG, "from client %d bytes", data_len);

    // set last use time
    con->last_use_time = btime_gettime();

    // move connection to front
    LinkedList1_Remove(&client->connections_list, &con->connections_list_node);
    LinkedList1_Append(&client->connections_list, &con->connections_list_node);

    // get buffer location
    uint8_t *out;
    if (!BufferWriter_StartPacket(&con->udp_send_writer, &out)) {
        connection_log(con, BLOG_ERROR, "out of UDP buffer");
        return 0;
    }

    // write message
    memcpy(out, data, data_len);

    // submit written message
    BufferWriter_EndPacket(&con->udp_send_writer, data_len);

    return 1;
}

void connection_close (struct connection *con)
{
    struct client *client = con->client;
    ASSERT(!con->closing)

    // if possible, free connection immediately
    if (!PacketPassFairQueueFlow_IsBusy(&con->send_qflow)) {
        connection_free(con);
        return;
    }

    connection_log(con, BLOG_DEBUG, "closing later");

    // decrement number of connections
    client->num_connections--;

    // remove from client's connections list
    LinkedList1_Remove(&client->connections_list, &con->connections_list_node);

    // remove from client's connections tree
    BAVL_Remove(&client->connections_tree, &con->connections_tree_node);

    // free UDP
    connection_free_udp(con);

    // insert to client's closing connections list
    LinkedList1_Append(&client->closing_connections_list, &con->closing_connections_list_node);

    // set busy handler
    PacketPassFairQueueFlow_SetBusyHandler(&con->send_qflow, (PacketPassFairQueue_handler_busy)connection_send_qflow_busy_handler, con);

    // unset first job
    BPending_Unset(&con->first_job);

    // set closing
    con->closing = 1;
}

void connection_send_qflow_busy_handler (struct connection *con)
{
    ASSERT(con->closing)
    PacketPassFairQueueFlow_AssertFree(&con->send_qflow);

    connection_log(con, BLOG_DEBUG, "closing finally");

    // free connection
    connection_free(con);
}

void connection_dgram_handler_event (struct connection *con, int event)
{
    ASSERT(!con->closing)

    connection_log(con, BLOG_INFO, "UDP error");

    // close connection
    connection_close(con);
}

void connection_udp_recv_if_handler_send (struct connection *con, uint8_t *data, int data_len)
{
    struct client *client = con->client;
    ASSERT(!con->closing)
    ASSERT(data_len >= 0)
    ASSERT(data_len <= udp_mtu)

    connection_log(con, BLOG_DEBUG, "from UDP %d bytes", data_len);

    // set last use time
    con->last_use_time = btime_gettime();

    // move connection to front
    LinkedList1_Remove(&client->connections_list, &con->connections_list_node);
    LinkedList1_Append(&client->connections_list, &con->connections_list_node);

    // accept packet
    PacketPassInterface_Done(&con->udp_recv_if);

    // send packet to client
    connection_send_to_client(con, 0, data, data_len);
}

struct connection * find_connection (struct client *client, uint16_t conid)
{
    BAVLNode *tree_node = BAVL_LookupExact(&client->connections_tree, &conid);
    if (!tree_node) {
        return NULL;
    }
    struct connection *con = UPPER_OBJECT(tree_node, struct connection, connections_tree_node);
    ASSERT(con->conid == conid)
    ASSERT(!con->closing)

    return con;
}

int uint16_comparator (void *unused, uint16_t *v1, uint16_t *v2)
{
    return B_COMPARE(*v1, *v2);
}

void maybe_update_dns (void)
{
#ifndef BADVPN_USE_WINAPI
    btime_t now = btime_gettime();
    if (now < btime_add(last_dns_update_time, DNS_UPDATE_TIME)) {
        return;
    }
    last_dns_update_time = now;
    BLog(BLOG_DEBUG, "update dns");

    if (res_init() != 0) {
        BLog(BLOG_ERROR, "res_init failed");
        goto fail;
    }

    if (_res.nscount == 0) {
        BLog(BLOG_ERROR, "no name servers available");
        goto fail;
    }

    BAddr addr;
    BAddr_InitIPv4(&addr, _res.nsaddr_list[0].sin_addr.s_addr, hton16(53));

    if (!BAddr_Compare(&addr, &dns_addr)) {
        char str[BADDR_MAX_PRINT_LEN];
        BAddr_Print(&addr, str);
        BLog(BLOG_INFO, "using DNS server %s", str);
    }

    dns_addr = addr;
    return;

fail:
    BAddr_InitNone(&dns_addr);
#endif
}
