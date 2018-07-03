#include <protocol/packetproto.h>
#include <misc/debug.h>

#include <flow/PacketProtoFlowPassthru.h>

int PacketProtoFlowPassthru_Init (PacketProtoFlowPassthru *o, int input_mtu, int num_packets, PacketPassInterface *output, BPendingGroup *pg)
{
    ASSERT(input_mtu >= 0)
    ASSERT(input_mtu <= PACKETPROTO_MAXPAYLOAD)
    ASSERT(num_packets > 0)
    ASSERT(PacketPassInterface_GetMTU(output) >= input_mtu)

    // init async input
    BufferWriter_Init(&o->ainput, input_mtu, pg);

    // init buffer
    if (!PacketBuffer_Init(&o->buffer, BufferWriter_GetOutput(&o->ainput), output, num_packets, pg)) {
        goto fail0;
    }

    DebugObject_Init(&o->d_obj);

    return 1;

fail0:
    BufferWriter_Free(&o->ainput);
    return 0;
}

void PacketProtoFlowPassthru_Free (PacketProtoFlowPassthru *o)
{
    DebugObject_Free(&o->d_obj);

    // free buffer
    PacketBuffer_Free(&o->buffer);

    // free async input
    BufferWriter_Free(&o->ainput);
}

BufferWriter * PacketProtoFlowPassthru_GetInput (PacketProtoFlowPassthru *o)
{
    DebugObject_Access(&o->d_obj);

    return &o->ainput;
}
