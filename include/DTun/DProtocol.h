#ifndef _DTUN_DPROTOCOL_H_
#define _DTUN_DPROTOCOL_H_

#include "DTun/Types.h"

namespace DTun
{
    #define DPROTOCOL_MSG_HELLO 0x0
    #define DPROTOCOL_MSG_HELLO_PROBE 0x1
    #define DPROTOCOL_MSG_HELLO_FAST 0x2
    #define DPROTOCOL_MSG_HELLO_SYMM 0x3
    #define DPROTOCOL_MSG_CONN_CREATE 0x4
    #define DPROTOCOL_MSG_CONN_CLOSE 0x5
    #define DPROTOCOL_MSG_PROBE 0x6
    #define DPROTOCOL_MSG_CONN 0x7
    #define DPROTOCOL_MSG_CONN_STATUS 0x8
    #define DPROTOCOL_MSG_FAST 0x9
    #define DPROTOCOL_MSG_SYMM 0xA
    #define DPROTOCOL_MSG_READY 0xB
    #define DPROTOCOL_MSG_NEXT 0xC

    #define DPROTOCOL_STATUS_PENDING 0x0
    #define DPROTOCOL_STATUS_ESTABLISHED 0x1
    #define DPROTOCOL_STATUS_ERR_CANCELED 0x10
    #define DPROTOCOL_STATUS_ERR_UNKNOWN 0x11
    #define DPROTOCOL_STATUS_ERR_NOTFOUND 0x12
    // Both peers behind a symmetrical NAT, no way to connect (at least for now)
    #define DPROTOCOL_STATUS_ERR_SYMM 0x13

    // Rendezvous modes

    // Fast mode, always use single port
    #define DPROTOCOL_RMODE_FAST 0x0
    // Symmetrical NAT connector, use spread connect, wait for port updates
    #define DPROTOCOL_RMODE_SYMM_CONN 0x1
    // Symmetrical NAT acceptor, use window ping, send port updates
    #define DPROTOCOL_RMODE_SYMM_ACC 0x2

    #pragma pack(1)
    struct DProtocolConnId
    {
        UInt32 nodeId;
        UInt32 connIdx;
    };

    struct DProtocolHeader
    {
        UInt8 msgCode;
    };

    // OUT MSGS

    struct DProtocolMsgHello
    {
        UInt32 nodeId;
        UInt32 probeIp;
        UInt16 probePort;
    };

    struct DProtocolMsgHelloFast
    {
        UInt32 nodeId;
        DProtocolConnId connId;
    };

    struct DProtocolMsgHelloSymm
    {
        UInt32 nodeId;
        DProtocolConnId connId;
    };

    struct DProtocolMsgConnCreate
    {
        DProtocolConnId connId;
        UInt32 dstNodeId;
        UInt32 remoteIp;
        UInt16 remotePort;
        UInt8 bestEffort;
    };

    struct DProtocolMsgConnClose
    {
        DProtocolConnId connId;
        UInt8 established;
    };

    // IN MSGS

    struct DProtocolMsgProbe
    {
        UInt32 srcIp;
        UInt16 srcPort;
    };

    struct DProtocolMsgConn
    {
        DProtocolConnId connId;
        UInt32 ip;
        UInt16 port;
        UInt8 mode;
        UInt32 srcIp;
        UInt8 bestEffort;
    };

    struct DProtocolMsgConnStatus
    {
        DProtocolConnId connId;
        UInt8 mode;
        UInt8 statusCode;
        UInt32 dstIp;
    };

    struct DProtocolMsgFast
    {
        DProtocolConnId connId;
        UInt32 nodeIp;
        UInt16 nodePort;
    };

    struct DProtocolMsgSymm
    {
        DProtocolConnId connId;
        UInt32 nodeIp;
        UInt16 nodePort;
    };

    // IN/OUT MSGS

    struct DProtocolMsgReady
    {
        DProtocolConnId connId;
    };

    struct DProtocolMsgNext
    {
        DProtocolConnId connId;
    };
    #pragma pack()

    inline DProtocolConnId toProtocolConnId(const ConnId& connId)
    {
        DProtocolConnId res;

        res.nodeId = connId.nodeId;
        res.connIdx = connId.connIdx;

        return res;
    }

    inline ConnId fromProtocolConnId(const DProtocolConnId& connId)
    {
        return ConnId(connId.nodeId, connId.connIdx);
    }
}

#endif
