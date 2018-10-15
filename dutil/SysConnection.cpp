#include "DTun/SysConnection.h"
#include "DTun/SysReactor.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <sys/epoll.h>

namespace DTun
{
    SysConnection::SysConnection(SysReactor& reactor, const boost::shared_ptr<SysHandle>& handle)
    : SysHandler(reactor, handle)
    {
        reactor.add(this);
    }

    SysConnection::~SysConnection()
    {
        close();
    }

    void SysConnection::write(const char* first, const char* last, const WriteCallback& callback)
    {
        WriteReq req;

        req.first = first;
        req.last = last;
        req.callback = callback;
        req.destIp = 0;
        req.destPort = 0;

        {
            boost::mutex::scoped_lock lock(m_);
            writeQueue_.push_back(req);
        }

        reactor().update(this);
    }

    void SysConnection::read(char* first, char* last, const ReadCallback& callback, bool readAll)
    {
        assert(!readAll);
        if (readAll) {
            LOG4CPLUS_FATAL(logger(), "realAll not supported!");
            return;
        }

        ReadReq req;

        req.first = first;
        req.last = last;
        req.callback = callback;
        req.drain = false;

        {
            boost::mutex::scoped_lock lock(m_);
            readQueue_.push_back(req);
        }

        reactor().update(this);
    }

    void SysConnection::writeTo(const char* first, const char* last, UInt32 destIp, UInt16 destPort, const WriteCallback& callback)
    {
        WriteReq req;

        req.first = first;
        req.last = last;
        req.callback = callback;
        req.destIp = destIp;
        req.destPort = destPort;

        {
            boost::mutex::scoped_lock lock(m_);
            writeQueue_.push_back(req);
        }

        reactor().update(this);
    }

    void SysConnection::readFrom(char* first, char* last, const ReadFromCallback& callback, bool drain)
    {
        ReadReq req;

        req.first = first;
        req.last = last;
        req.fromCallback = callback;
        req.drain = drain;

        {
            boost::mutex::scoped_lock lock(m_);
            readQueue_.push_back(req);
        }

        reactor().update(this);
    }

    void SysConnection::close(bool immediate)
    {
        boost::shared_ptr<SysHandle> handle = reactor().remove(this);
        if (handle) {
            handle->close(immediate);
        }
    }

    int SysConnection::getPollEvents() const
    {
        int res = 0;

        boost::mutex::scoped_lock lock(m_);
        if (!writeQueue_.empty()) {
            res |= EPOLLOUT;
        }
        if (!readQueue_.empty()) {
            res |= EPOLLIN;
        }

        return res;
    }

    void SysConnection::handleRead()
    {
        ReadReq* req;

        {
            boost::mutex::scoped_lock lock(m_);
            assert(!readQueue_.empty());
            req = &readQueue_.front();
        }

        if (req->callback) {
            handleReadNormal(req);
        } else if (!req->drain) {
            handleReadFrom(req);
        } else {
            // capture 'handle', always check it first because
            // 'cb' might have deleted 'this'.
            boost::shared_ptr<SysHandle> handle = sysHandle();

            // read until UDP socket is drained or error occurs or
            // no outstanding read requests.

            while (true) {
                if (!handleReadFrom(req)) {
                    break;
                }
                if (handle->sock() == SYS_INVALID_SOCKET) {
                    break;
                }

                boost::mutex::scoped_lock lock(m_);
                if (readQueue_.empty()) {
                    break;
                }
                req = &readQueue_.front();
            }
        }
    }

    void SysConnection::handleWrite()
    {
        boost::shared_ptr<SysHandle> handle = sysHandle();

        WriteReq* req;

        {
            boost::mutex::scoped_lock lock(m_);
            assert(!writeQueue_.empty());
            req = &writeQueue_.front();
        }

        WriteCallback cb;

        int res;

        while (true) {
            if (req->destIp) {
                struct sockaddr_in sa;
                memset(&sa, 0, sizeof(sa));
                sa.sin_family = AF_INET;
                sa.sin_addr.s_addr = req->destIp;
                sa.sin_port = req->destPort;

                res = ::sendto(sysHandle()->sock(), req->first, req->last - req->first, 0, (const struct sockaddr*)&sa, sizeof(sa));
            } else {
                res = ::send(sysHandle()->sock(), req->first, req->last - req->first, 0);
            }

            if (res == -1) {
                int err = errno;
                LOG4CPLUS_TRACE(logger(), "Cannot write sys socket: " << err);

                cb = req->callback;

                {
                    boost::mutex::scoped_lock lock(m_);
                    writeQueue_.pop_front();
                }

                reactor().update(this);

                cb(err);
                return;
            }

            req->first += res;

            if (req->first >= req->last) {
                cb = req->callback;

                {
                    boost::mutex::scoped_lock lock(m_);
                    writeQueue_.pop_front();
                }

                reactor().update(this);

                cb(0);
            }

            if (handle->sock() == SYS_INVALID_SOCKET) {
                break;
            }

            boost::mutex::scoped_lock lock(m_);
            if (writeQueue_.empty()) {
                break;
            }
            req = &writeQueue_.front();
        }
    }

    void SysConnection::handleReadNormal(ReadReq* req)
    {
        ReadCallback cb;

        int res;
        if ((res = ::recv(sysHandle()->sock(), req->first, req->last - req->first, 0)) == -1) {
            int err = errno;
            LOG4CPLUS_TRACE(logger(), "Cannot read sys socket: " << strerror(err));

            cb = req->callback;

            {
                boost::mutex::scoped_lock lock(m_);
                readQueue_.pop_front();
            }

            reactor().update(this);

            cb(err, 0);
            return;
        }

        cb = req->callback;

        {
            boost::mutex::scoped_lock lock(m_);
            readQueue_.pop_front();
        }

        reactor().update(this);

        if (cb) {
            cb(0, res);
        }
    }

    bool SysConnection::handleReadFrom(ReadReq* req)
    {
        ReadFromCallback cb;

        struct sockaddr_in sa;
        socklen_t saLen = sizeof(sa);

        int res;
        if ((res = ::recvfrom(sysHandle()->sock(), req->first, req->last - req->first, (req->drain ? MSG_DONTWAIT : 0), (struct sockaddr*)&sa, &saLen)) == -1) {
            int err = errno;

            if (req->drain && (err == EAGAIN || err == EWOULDBLOCK)) {
                cb = req->fromCallback;

                {
                    boost::mutex::scoped_lock lock(m_);
                    readQueue_.pop_front();
                }

                reactor().update(this);

                cb(0, 0, 0, 0);

                return false;
            }

            LOG4CPLUS_TRACE(logger(), "Cannot readFrom sys socket: " << strerror(err));

            cb = req->fromCallback;

            {
                boost::mutex::scoped_lock lock(m_);
                readQueue_.pop_front();
            }

            reactor().update(this);

            cb(err, 0, 0, 0);
            return false;
        }

        cb = req->fromCallback;

        {
            boost::mutex::scoped_lock lock(m_);
            readQueue_.pop_front();
        }

        reactor().update(this);

        cb(0, res, sa.sin_addr.s_addr, sa.sin_port);

        return true;
    }
}
