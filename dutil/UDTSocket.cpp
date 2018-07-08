#include "DTun/UDTSocket.h"
#include "Logger.h"

namespace DTun
{
    UDTSocket::UDTSocket(UDTReactor& reactor, UDTSOCKET sock)
    : reactor_(reactor)
    , sock_(sock)
    , cookie_(0)
    {
    }

    UDTSocket::~UDTSocket()
    {
    }

    bool UDTSocket::getSockName(UInt32& ip, UInt16& port)
    {
        struct sockaddr_in addr;
        int addrLen = sizeof(addr);

        if (UDT::getsockname(sock_, (struct sockaddr*)&addr, &addrLen) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot get UDT sock name: " << UDT::getlasterror().getErrorMessage());
            return false;
        }

        ip = addr.sin_addr.s_addr;
        port = addr.sin_port;

        return true;
    }

    bool UDTSocket::getPeerName(UInt32& ip, UInt16& port)
    {
        struct sockaddr_in addr;
        int addrLen = sizeof(addr);

        if (UDT::getpeername(sock_, (struct sockaddr*)&addr, &addrLen) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot get UDT peer name: " << UDT::getlasterror().getErrorMessage());
            return false;
        }

        ip = addr.sin_addr.s_addr;
        port = addr.sin_port;

        return true;
    }
}
