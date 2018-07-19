#include "DTun/LTUDPConnector.h"
#include "DTun/LTUDPManager.h"
#include "DTun/SysConnection.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

namespace DTun
{
    LTUDPConnector::LTUDPConnector(const boost::shared_ptr<LTUDPHandle>& handle)
    : handedOut_(false)
    , handle_(handle)
    , watch_(boost::make_shared<OpWatch>(boost::ref(handle->reactor())))
    {
    }

    LTUDPConnector::~LTUDPConnector()
    {
        close();
    }

    void LTUDPConnector::close()
    {
        if (watch_->close() && !handedOut_) {
            handle_->close();
        }
    }

    bool LTUDPConnector::connect(const std::string& address, const std::string& port, const ConnectCallback& callback, Mode mode)
    {
        addrinfo hints;

        memset(&hints, 0, sizeof(struct addrinfo));

        hints.ai_flags = AI_PASSIVE;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* res;

        if (::getaddrinfo(address.c_str(), port.c_str(), &hints, &res) != 0) {
            LOG4CPLUS_ERROR(logger(), "cannot resolve address/port");
            return false;
        }

        UInt32 destIp = ((const struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr;
        UInt16 destPort = ((const struct sockaddr_in*)res->ai_addr)->sin_port;

        freeaddrinfo(res);

        handle_->reactor().post(watch_->wrap(
            boost::bind(&LTUDPConnector::onStartConnect, this, address, port, callback, mode, destIp, destPort)));

        return true;
    }

    void LTUDPConnector::onStartConnect(const std::string& address, const std::string& port, const ConnectCallback& callback, Mode mode,
        UInt32 destIp, UInt16 destPort)
    {
        if (mode != ModeRendezvousAcc) {
            handle_->impl()->connect(address, port,
                watch_->wrap<int>(boost::bind(&LTUDPConnector::onConnect, this, _1, callback)));
        } else {
            boost::shared_ptr<SConnection> conn = handle_->impl()->conn();

            boost::shared_ptr<SysConnection> conn2 = boost::dynamic_pointer_cast<SysConnection>(conn);
            assert(conn2);

            int sndb = 0;
            socklen_t optlen = sizeof(sndb);
            if (::getsockopt(conn2->sysHandle()->sock(), SOL_SOCKET, SO_SNDBUF, &sndb, &optlen) == SYS_SOCKET_ERROR) {
                LOG4CPLUS_ERROR(logger(), "cannot get sndb");
            }

            int rcvb = 0;
            optlen = sizeof(rcvb);
            if (::getsockopt(conn2->sysHandle()->sock(), SOL_SOCKET, SO_RCVBUF, &rcvb, &optlen) == SYS_SOCKET_ERROR) {
                LOG4CPLUS_ERROR(logger(), "cannot get rcvb");
            }

            LOG4CPLUS_INFO(logger(), "sndb: " << sndb << ", rcvb: " << rcvb);

            sndb = 4 * 1024 * 1024;
            if (::setsockopt(conn2->sysHandle()->sock(), SOL_SOCKET, SO_SNDBUF, &sndb, sizeof(sndb)) == SYS_SOCKET_ERROR) {
                LOG4CPLUS_ERROR(logger(), "cannot set sndb");
            }

            rcvb = 4 * 1024 * 1024;
            if (::setsockopt(conn2->sysHandle()->sock(), SOL_SOCKET, SO_RCVBUF, &rcvb, sizeof(rcvb)) == SYS_SOCKET_ERROR) {
                LOG4CPLUS_ERROR(logger(), "cannot set rcvb");
            }

            int ttl = 3; //15 (3)
            if (::setsockopt(conn2->sysHandle()->sock(), IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) == SYS_SOCKET_ERROR) {
                LOG4CPLUS_ERROR(logger(), "cannot set ttl");
            }

            srand(time(NULL));

            handle_->impl()->listen(1,
                watch_->wrap<boost::shared_ptr<SHandle> >(boost::bind(&LTUDPConnector::onRendezvousAccept, this, _1, callback)));
            //onRendezvousTimeout(120, 100, destIp, destPort, callback);
            handle_->reactor().post(watch_->wrap(
                boost::bind(&LTUDPConnector::onRendezvousTimeout, this, 6500, 10, destIp, destPort, callback)), 0);
        }
    }

    void LTUDPConnector::onConnect(int err, const ConnectCallback& callback)
    {
        handedOut_ = true;
        callback(err);
    }

    void LTUDPConnector::onRendezvousAccept(const boost::shared_ptr<SHandle>& handle, const ConnectCallback& callback)
    {
        if (handedOut_) {
            return;
        }

        boost::shared_ptr<LTUDPHandle> ltudpHandle =
            boost::dynamic_pointer_cast<LTUDPHandle>(handle);
        assert(ltudpHandle);

        handle_.reset();
        handle_ = ltudpHandle;

        handedOut_ = true;
        callback(0);
    }

    void LTUDPConnector::onRendezvousTimeout(int count, int timeoutMs, UInt32 destIp, UInt16 destPort, const ConnectCallback& callback)
    {
        //LOG4CPLUS_TRACE(logger(), "LTUDPConnector::onRendezvousTimeout(" << count << ")");

        if (handedOut_) {
            return;
        }

        if (count == 0) {
            LOG4CPLUS_TRACE(logger(), "FCKING AGAIN!");
            //handle_->reactor().post(watch_->wrap(
              //  boost::bind(&LTUDPConnector::onRendezvousTimeout, this, 650, 1, destIp, destPort, callback)), 0);


            //handedOut_ = true;
            //callback(ERR_TIMEOUT);
        } else {
            UInt32 fromIp;
            UInt16 fromPort;

            bool res = handle_->getSockName(fromIp, fromPort);
            assert(res);

            //LOG4CPLUS_TRACE(logger(), "RDZV from " << ipPortToString(fromIp, fromPort) << " to " << ipPortToString(destIp, destPort));

            destPort = 0;
            for (int probe = (int)lwip_ntohs(destPort) + 10 * (6500 - count); probe < (int)lwip_ntohs(destPort) + 10 * (6500 - count + 1); ++probe) {
                if ((probe >= 1024) && (probe <= 65535) && (probe != 5351) && (probe != 51413)) {
                    handle_->impl()->rendezvousPing(destIp, lwip_htons(probe));
                    //LOG4CPLUS_TRACE(logger(), "RDZV from " << ipPortToString(fromIp, fromPort) << " to " << ipPortToString(destIp, lwip_htons(probe)));
                    //handle_->impl()->rendezvousPing(destIp, lwip_htons(60000 - probe));
                }
            }

            //destPort = 0;
            //for (int probe = (int)lwip_ntohs(destPort) - 500; probe <= (int)lwip_ntohs(destPort) + 500; ++probe) {
                //if ((probe >= 1) && (probe <= 65535)) {
                    //handle_->impl()->rendezvousPing(destIp, lwip_htons(probe));
                //}
            //}

            //handle_->impl()->rendezvousPing(destIp, destPort);

            /*std::set<uint16_t> ports;

            ports.clear();

            std::vector<uint16_t> v;

            while (ports.size() < 600) {
                uint16_t p = 1024 + rand() % (65535 - 1024);
                if (ports.insert(p).second) {
                    v.push_back(p);
                }
            }
            ports.insert(destPort);

            for (std::vector<uint16_t>::const_iterator it = v.begin(); it != v.end(); ++it) {
                handle_->impl()->rendezvousPing(destIp, lwip_htons(*it));
            }*/

            if (count == 1) {
                LOG4CPLUS_TRACE(logger(), "DONE!");
                timeoutMs = 80000;
            } else {
                if ((count % 100) == 0) {
                    //timeoutMs = 1000;
                    timeoutMs = 1;
                } else {
                    timeoutMs = 1;
                }
            }

            handle_->reactor().post(watch_->wrap(
                boost::bind(&LTUDPConnector::onRendezvousTimeout, this, count - 1, timeoutMs, destIp, destPort, callback)), timeoutMs);
        }
    }
}
