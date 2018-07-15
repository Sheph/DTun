#include "DTun/SysManager.h"
#include "DTun/SysHandle.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <boost/make_shared.hpp>
#include <fcntl.h>

namespace DTun
{
    SysManager::SysManager(SysReactor& reactor)
    : reactor_(reactor)
    {
    }

    SysManager::~SysManager()
    {
    }

    SReactor& SysManager::reactor()
    {
        return reactor_;
    }

    boost::shared_ptr<SHandle> SysManager::createStreamSocket()
    {
        SYSSOCKET sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock == SYS_INVALID_SOCKET) {
            LOG4CPLUS_ERROR(logger(), "Cannot create TCP socket: " << strerror(errno));
            return boost::shared_ptr<SHandle>();
        }
        return boost::make_shared<SysHandle>(boost::ref(reactor_), sock);
    }

    boost::shared_ptr<SHandle> SysManager::createDatagramSocket(SYSSOCKET sock)
    {
        if (sock == SYS_INVALID_SOCKET) {
            sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (sock == SYS_INVALID_SOCKET) {
                LOG4CPLUS_ERROR(logger(), "Cannot create UDP socket: " << strerror(errno));
                return boost::shared_ptr<SHandle>();
            }
        }

        if (::fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
            LOG4CPLUS_ERROR(logger(), "cannot set sock non-blocking");
            closeSysSocketChecked(sock);
            return boost::shared_ptr<SHandle>();
        }

        return boost::make_shared<SysHandle>(boost::ref(reactor_), sock);
    }
}
