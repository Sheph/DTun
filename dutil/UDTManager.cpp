#include "DTun/UDTManager.h"
#include "DTun/UDTHandle.h"
#include "Logger.h"
#include <boost/make_shared.hpp>

namespace DTun
{
    UDTManager::UDTManager(UDTReactor& reactor)
    : reactor_(reactor)
    {
    }

    UDTManager::~UDTManager()
    {
    }

    SReactor& UDTManager::reactor()
    {
        return reactor_;
    }

    boost::shared_ptr<SHandle> UDTManager::createStreamSocket()
    {
        UDTSOCKET sock = UDT::socket(AF_INET, SOCK_STREAM, 0);
        if (sock == UDT::INVALID_SOCK) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDT socket: " << UDT::getlasterror().getErrorMessage());
            return boost::shared_ptr<SHandle>();
        }
        return boost::make_shared<UDTHandle>(boost::ref(reactor_), sock);
    }

    boost::shared_ptr<SHandle> UDTManager::createDatagramSocket(SYSSOCKET s)
    {
        assert(false);
        return boost::shared_ptr<SHandle>();
    }

    void UDTManager::enablePortRemap(UInt16 dstPort)
    {
    }
}
