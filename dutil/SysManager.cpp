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

            socklen_t optvalLen = sizeof(int);

            int bufSize = 0;
            if (::getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &bufSize, &optvalLen) < 0) {
                LOG4CPLUS_ERROR(logger(), "Cannot getsockopt UDP socket: " << strerror(errno));
                closeSysSocketChecked(sock);
                return boost::shared_ptr<SHandle>();
            }

            int optval = 208 * 1024 * 2;

            if (bufSize < optval) {
                if (::setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval)) < 0) {
                    LOG4CPLUS_ERROR(logger(), "Cannot set UDP SO_RCVBUF to " << optval);
                    closeSysSocketChecked(sock);
                    return boost::shared_ptr<SHandle>();
                }

                if (::getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &bufSize, &optvalLen) < 0) {
                    LOG4CPLUS_ERROR(logger(), "Cannot getsockopt UDP socket: " << strerror(errno));
                    closeSysSocketChecked(sock);
                    return boost::shared_ptr<SHandle>();
                }

                if (bufSize < optval) {
                    LOG4CPLUS_ERROR(logger(), "UDP socket SO_RCVBUF is " << bufSize << " < " << optval);
                    closeSysSocketChecked(sock);
                    return boost::shared_ptr<SHandle>();
                }
            }

            if (::getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &bufSize, &optvalLen) < 0) {
                LOG4CPLUS_ERROR(logger(), "Cannot getsockopt UDP socket: " << strerror(errno));
                closeSysSocketChecked(sock);
                return boost::shared_ptr<SHandle>();
            }

            optval = 208 * 1024 * 2;

            if (bufSize < optval) {
                if (::setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval)) < 0) {
                    LOG4CPLUS_ERROR(logger(), "Cannot set UDP SO_SNDBUF to " << optval);
                    closeSysSocketChecked(sock);
                    return boost::shared_ptr<SHandle>();
                }

                if (::getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &bufSize, &optvalLen) < 0) {
                    LOG4CPLUS_ERROR(logger(), "Cannot getsockopt UDP socket: " << strerror(errno));
                    closeSysSocketChecked(sock);
                    return boost::shared_ptr<SHandle>();
                }

                if (bufSize < optval) {
                    LOG4CPLUS_ERROR(logger(), "UDP socket SO_SNDBUF is " << bufSize << " < " << optval);
                    closeSysSocketChecked(sock);
                    return boost::shared_ptr<SHandle>();
                }
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
