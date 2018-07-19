#include "DTun/LTUDPConnector.h"
#include "DTun/LTUDPManager.h"
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
            conns_.clear();
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
            srand(time(NULL));

            handle_->impl()->connect(address, port,
                watch_->wrap<int>(boost::bind(&LTUDPConnector::onConnect, this, _1, callback)));

            if (mode == ModeRendezvousConn) {
                conns_.clear();
                for (int i = 0; i < 600; ++i) {
                    boost::shared_ptr<SHandle> handle = handle_->impl()->mgr().innerMgr().createDatagramSocket();
                    struct sockaddr_in addr;

                    memset(&addr, 0, sizeof(addr));
                    addr.sin_family = AF_INET;
                    addr.sin_addr.s_addr = htonl(INADDR_ANY);

                    if (!handle->bind((const struct sockaddr*)&addr, sizeof(addr))) {
                        break;
                    }

                    conns_.push_back(handle->createConnection());
                }

                std::vector<uint16_t> ports;

                onConnTimeout(500, ports, 0, destIp, destPort);
            }
        } else {
            handle_->impl()->listen(1,
                watch_->wrap<boost::shared_ptr<SHandle> >(boost::bind(&LTUDPConnector::onRendezvousAccept, this, _1, callback)));
            onRendezvousTimeout(6, 3000, destIp, destPort, callback);
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
        LOG4CPLUS_TRACE(logger(), "LTUDPConnector::onRendezvousTimeout(" << count << ")");

        if (handedOut_) {
            return;
        }

        if (count == 0) {
            handedOut_ = true;
            callback(ERR_TIMEOUT);
        } else {
            handle_->impl()->rendezvousPing(destIp, destPort);
            handle_->reactor().post(watch_->wrap(
                boost::bind(&LTUDPConnector::onRendezvousTimeout, this, count - 1, timeoutMs, destIp, destPort, callback)), timeoutMs);
        }
    }

    void LTUDPConnector::onConnTimeout(int timeoutMs, std::vector<uint16_t> v, int cnt, UInt32 destIp, UInt16 destPort)
    {
        if (handedOut_) {
            return;
        }

        if (cnt == 0) {
            cnt = 3;
            v.clear();

            std::set<uint16_t> ports;

            while (ports.size() < 6) {
                uint16_t p = 1024 + rand() % (65535 - 1024);
                if (ports.insert(p).second) {
                    v.push_back(lwip_htons(p));
                }
            }
            v.push_back(destPort);
        }

        boost::shared_ptr<std::vector<char> > sndBuff =
            boost::make_shared<std::vector<char> >(4);

        (*sndBuff)[0] = 0xAA;
        (*sndBuff)[1] = 0xBB;
        (*sndBuff)[2] = 0xCC;
        (*sndBuff)[3] = 0xDD;

        for (std::vector<boost::shared_ptr<SConnection> >::const_iterator it = conns_.begin(); it != conns_.end(); ++it) {

            for (std::vector<uint16_t>::const_iterator jt = v.begin(); jt != v.end(); ++jt) {
                UInt32 sip;
                UInt16 sport;
                (*it)->handle()->getSockName(sip, sport);
                LOG4CPLUS_TRACE(logger(), "Rendezvous ping to " << ipPortToString(destIp, *jt) << " from " << ipPortToString(sip, sport));
                (*it)->writeTo(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
                    destIp, *jt,
                    boost::bind(&LTUDPHandleImpl::onRendezvousPingSend, _1, sndBuff));
            }
        }

        handle_->reactor().post(watch_->wrap(
            boost::bind(&LTUDPConnector::onConnTimeout, this, timeoutMs, v, cnt - 1, destIp, destPort)), timeoutMs);
    }
}
