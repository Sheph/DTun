#include "DTun/TCPConnection.h"
#include "DTun/TCPReactor.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <sys/epoll.h>

namespace DTun
{
    TCPConnection::TCPConnection(TCPReactor& reactor, SYSSOCKET sock)
    : TCPSocket(reactor, sock)
    {
        reactor.add(this);
    }

    TCPConnection::~TCPConnection()
    {
        close();
    }

    void TCPConnection::write(const char* first, const char* last, const WriteCallback& callback)
    {
        WriteReq req;

        req.first = first;
        req.last = last;
        req.callback = callback;

        {
            boost::mutex::scoped_lock lock(m_);
            writeQueue_.push_back(req);
        }

        reactor().update(this);
    }

    void TCPConnection::read(char* first, char* last, const ReadCallback& callback)
    {
        ReadReq req;

        req.first = first;
        req.last = last;
        req.callback = callback;

        {
            boost::mutex::scoped_lock lock(m_);
            readQueue_.push_back(req);
        }

        reactor().update(this);
    }

    void TCPConnection::close()
    {
        SYSSOCKET s = reactor().remove(this);
        if (s != SYS_INVALID_SOCKET) {
            DTun::closeSysSocketChecked(s);
        }
    }

    int TCPConnection::getPollEvents() const
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

    void TCPConnection::handleRead()
    {
        ReadReq* req;

        {
            boost::mutex::scoped_lock lock(m_);
            assert(!readQueue_.empty());
            req = &readQueue_.front();
        }

        ReadCallback cb;

        int res;
        if ((res = ::recv(sock(), req->first, req->last - req->first, 0)) == -1) {
            int err = errno;
            LOG4CPLUS_TRACE(logger(), "Cannot read TCP socket: " << strerror(err));

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

    void TCPConnection::handleWrite()
    {
        WriteReq* req;

        {
            boost::mutex::scoped_lock lock(m_);
            assert(!writeQueue_.empty());
            req = &writeQueue_.front();
        }

        WriteCallback cb;

        int res;
        if ((res = ::send(sock(), req->first, req->last - req->first, 0)) == -1) {
            int err = errno;
            LOG4CPLUS_TRACE(logger(), "Cannot write TCP socket: " << err);

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
    }
}
