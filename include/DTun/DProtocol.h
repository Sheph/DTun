#ifndef _DTUN_DPROTOCOL_H_
#define _DTUN_DPROTOCOL_H_

#include "DTun/Types.h"

namespace DTun
{
    #define DPROTOCOL_MSG_PROBE 0x0
    #define DPROTOCOL_MSG_HELLO 0x1
    #define DPROTOCOL_MSG_HELLO_CONN 0x2
    #define DPROTOCOL_MSG_HELLO_ACC 0x3
    #define DPROTOCOL_MSG_HELLO_SYMM_NEXT 0x4
    #define DPROTOCOL_MSG_SYMM_DONE_OUT 0x5
    #define DPROTOCOL_MSG_PROBE_RESULT 0x6
    #define DPROTOCOL_MSG_CONN 0x7
    #define DPROTOCOL_MSG_CONN_ERR 0x8
    #define DPROTOCOL_MSG_CONN_OK 0x9
    #define DPROTOCOL_MSG_SYMM_NEXT 0x10
    #define DPROTOCOL_MSG_SYMM_DONE_IN 0x11

    #define DPROTOCOL_ERR_NONE 0x0
    #define DPROTOCOL_ERR_UNKNOWN 0x1
    #define DPROTOCOL_ERR_NOTFOUND 0x2
    // Both peers behind a symmetrical NAT, no way to connect (at least for now)
    #define DPROTOCOL_ERR_SYMM 0x3

    // Rendezvous roles

    // Normal connector, always try single port
    #define DPROTOCOL_ROLE_CONN 0x0
    // Symmetrical NAT connector, use spread connect, use port update
    #define DPROTOCOL_ROLE_CONN_SYMM 0x1
    // Normal acceptor, send pings, wait for connect
    #define DPROTOCOL_ROLE_ACC 0x2
    // Symmetrical NAT acceptor, use window ping, use port update
    #define DPROTOCOL_ROLE_ACC_SYMM 0x3

    #pragma pack(1)
    struct DProtocolHeader
    {
        UInt8 msgCode;
    };

    // OUT MSGS

    struct DProtocolMsgProbe
    {
        UInt8 dummy;
    };

    struct DProtocolMsgHello
    {
        UInt32 nodeId;
        UInt32 probeIp;
        UInt16 probePort;
    };

    struct DProtocolMsgHelloConn
    {
        UInt32 srcNodeId;
        UInt32 dstNodeId;
        UInt32 connId;
        UInt32 remoteIp;
        UInt16 remotePort;
    };

    struct DProtocolMsgHelloAcc
    {
        UInt32 srcNodeId;
        UInt32 dstNodeId;
        UInt32 connId;
    };

    struct DProtocolMsgHelloSymmNext
    {
        UInt32 srcNodeId;
        UInt32 dstNodeId;
        UInt32 connId;
        UInt8 failed;
    };

    struct DProtocolMsgSymmDoneOut
    {
        UInt32 dstNodeId;
        UInt32 connId;
    };

    // IN MSGS

    struct DProtocolMsgProbeResult
    {
        UInt32 srcIp;
        UInt16 srcPort;
    };

    struct DProtocolMsgConn
    {
        UInt32 srcNodeId;
        UInt32 srcNodeIp;
        UInt16 srcNodePort;
        UInt32 connId;
        UInt32 ip;
        UInt16 port;
        UInt8 role;
    };

    struct DProtocolMsgConnErr
    {
        UInt32 connId;
        UInt32 errCode;
    };

    struct DProtocolMsgConnOK
    {
        UInt32 connId;
        UInt32 dstNodeIp;
        UInt16 dstNodePort;
        UInt8 role;
    };

    struct DProtocolMsgSymmNext
    {
        UInt32 connId;
        UInt16 port;
    };

    struct DProtocolMsgSymmDoneIn
    {
        UInt32 srcNodeId;
        UInt32 connId;
    };
    #pragma pack()
}

#endif
