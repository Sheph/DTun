#include "DTun/TCPSocket.h"
#include "Logger.h"

namespace DTun
{
    TCPSocket::TCPSocket(TCPReactor& reactor, SYSSOCKET sock)
    : reactor_(reactor)
    , sock_(sock)
    , cookie_(0)
    {
    }

    TCPSocket::~TCPSocket()
    {
    }
}
