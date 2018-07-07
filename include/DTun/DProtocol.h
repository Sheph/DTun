#ifndef _DTUN_DPROTOCOL_H_
#define _DTUN_DPROTOCOL_H_

#include "DTun/Types.h"

namespace DTun
{
    #define DPROTOCOL_MSG_HELLO 0x0
    #define DPROTOCOL_MSG_HELLO_CONN 0x1
    #define DPROTOCOL_MSG_HELLO_ACC 0x2
    #define DPROTOCOL_MSG_CONN 0x3
    #define DPROTOCOL_MSG_CONN_ERR 0x4
    #define DPROTOCOL_MSG_CONN_OK 0x5

    #pragma pack(1)
    struct DProtocolHeader
    {
        UInt8 msgCode;
    };

    // OUT MSGS

    struct DProtocolMsgHello
    {
        UInt32 nodeId;
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
        UInt32 connId;
    };

    // IN MSGS

    struct DProtocolMsgConn
    {
        UInt32 srcNodeId;
        UInt32 srcNodeIp;
        UInt16 srcNodePort;
        UInt32 connId;
        UInt32 ip;
        UInt16 port;
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
    };
    #pragma pack()
}

#endif
