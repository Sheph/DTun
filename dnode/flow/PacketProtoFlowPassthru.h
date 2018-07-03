#ifndef BADVPN_FLOW_PACKETPROTOFLOWPASSTHRU_H
#define BADVPN_FLOW_PACKETPROTOFLOWPASSTHRU_H

#include <misc/debug.h>

#include <base/DebugObject.h>
#include <flow/BufferWriter.h>
#include <flow/PacketBuffer.h>

typedef struct {
    BufferWriter ainput;
    PacketBuffer buffer;
    DebugObject d_obj;
} PacketProtoFlowPassthru;

int PacketProtoFlowPassthru_Init (PacketProtoFlowPassthru *o, int input_mtu, int num_packets, PacketPassInterface *output, BPendingGroup *pg) WARN_UNUSED;

void PacketProtoFlowPassthru_Free (PacketProtoFlowPassthru *o);

BufferWriter * PacketProtoFlowPassthru_GetInput (PacketProtoFlowPassthru *o);

#endif
