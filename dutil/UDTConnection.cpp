#include "DTun/UDTConnection.h"
#include "DTun/UDTReactor.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>

namespace DTun
{
    UDTConnection::UDTConnection(UDTReactor& reactor, const boost::shared_ptr<UDTHandle>& handle)
    : UDTHandler(reactor, handle)
    {
        reactor.add(this);
    }

    UDTConnection::~UDTConnection()
    {
        close();
    }

    void UDTConnection::write(const char* first, const char* last, const WriteCallback& callback)
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

    void UDTConnection::read(char* first, char* last, const ReadCallback& callback, bool readAll)
    {
        ReadReq req;

        req.first = first;
        req.last = last;
        req.total_read = 0;
        req.callback = callback;
        req.readAll = readAll;

        {
            boost::mutex::scoped_lock lock(m_);
            readQueue_.push_back(req);
        }

        reactor().update(this);
    }

    void UDTConnection::writeTo(const char* first, const char* last, UInt32 destIp, UInt16 destPort, const WriteCallback& callback)
    {
        assert(false);
        LOG4CPLUS_FATAL(logger(), "writeTo not supported!");
    }

    void UDTConnection::readFrom(char* first, char* last, const ReadFromCallback& callback)
    {
        assert(false);
        LOG4CPLUS_FATAL(logger(), "readFrom not supported!");
    }

    void UDTConnection::close()
    {
        boost::shared_ptr<UDTHandle> handle = reactor().remove(this);
        if (handle) {
            handle->close();
        }
    }

    int UDTConnection::getPollEvents() const
    {
        int res = 0;

        boost::mutex::scoped_lock lock(m_);
        if (!writeQueue_.empty()) {
            res |= UDT_EPOLL_OUT;
        }
        if (!readQueue_.empty()) {
            res |= UDT_EPOLL_IN;
        }

        return res;
    }

    void UDTConnection::handleRead()
    {
        ReadReq* req;

        {
            boost::mutex::scoped_lock lock(m_);
            assert(!readQueue_.empty());
            req = &readQueue_.front();
        }

        ReadCallback cb;
        int total_read;

        int res;
        if ((res = UDT::recv(udtHandle()->sock(), req->first, req->last - req->first, 0)) == UDT::ERROR) {
            LOG4CPLUS_TRACE(logger(), "Cannot read UDT socket: " << UDT::getlasterror().getErrorMessage());

            cb = req->callback;
            total_read = req->total_read;

            {
                boost::mutex::scoped_lock lock(m_);
                readQueue_.pop_front();
            }

            int err = UDT::getlasterror().getErrorCode();

            reactor().update(this);

            cb(err, total_read);
            return;
        }

        req->first += res;
        req->total_read += res;

        if (!req->readAll || (req->first >= req->last)) {
            cb = req->callback;
            total_read = req->total_read;
            {
                boost::mutex::scoped_lock lock(m_);
                readQueue_.pop_front();
            }
            reactor().update(this);
        }

        if (cb) {
            cb(0, total_read);
        }
    }

    void UDTConnection::handleWrite()
    {
        WriteReq* req;

        {
            boost::mutex::scoped_lock lock(m_);
            assert(!writeQueue_.empty());
            req = &writeQueue_.front();
        }

        WriteCallback cb;

        int res;
        if ((res = UDT::send(udtHandle()->sock(), req->first, req->last - req->first, 0)) == UDT::ERROR) {
            LOG4CPLUS_TRACE(logger(), "Cannot write UDT socket: " << UDT::getlasterror().getErrorMessage());

            cb = req->callback;

            {
                boost::mutex::scoped_lock lock(m_);
                writeQueue_.pop_front();
            }

            int err = UDT::getlasterror().getErrorCode();

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
