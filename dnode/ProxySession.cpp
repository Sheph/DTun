#include "ProxySession.h"

namespace DNode
{
    ProxySession::ProxySession(DTun::UDTReactor& udtReactor, DTun::TCPReactor& tcpReactor)
    : udtReactor_(udtReactor)
    , tcpReactor_(tcpReactor)
    {
    }

    ProxySession::~ProxySession()
    {
    }

    bool ProxySession::start(DTun::UInt32 localIp, DTun::UInt16 localPort,
        DTun::UInt32 remoteIp, DTun::UInt16 remotePort, const DoneCallback& callback)
    {
        return false;
    }
}
