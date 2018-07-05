#include "DTun/UDTConnection.h"
#include "DTun/UDTReactor.h"
#include "Logger.h"
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>

namespace DTun
{
    UDTConnection::UDTConnection(UDTReactor& reactor, UDTSOCKET sock)
    : UDTSocket(reactor, sock)
    , closed_(false)
    {
        reactor.add(this);
    }

    UDTConnection::~UDTConnection()
    {
    }

    bool UDTConnection::write(const char* first, const char* last, const WriteCallback& callback)
    {
        WriteReq req;

        req.first = first;
        req.last = last;
        req.callback = callback;

        {
            boost::mutex::scoped_lock lock(m_);
            if (closed_) {
                return false;
            }
            writeQueue_.push_back(req);
        }

        reactor().update(this);

        return true;
    }

    bool UDTConnection::read(char* first, char* last, const ReadCallback& callback, bool readAll)
    {
        ReadReq req;

        req.first = first;
        req.last = last;
        req.total_read = 0;
        req.callback = callback;
        req.readAll = readAll;

        {
            boost::mutex::scoped_lock lock(m_);
            if (closed_) {
                return false;
            }
            readQueue_.push_back(req);
        }

        reactor().update(this);

        return true;
    }

    void UDTConnection::close()
    {
        {
            boost::mutex::scoped_lock lock(m_);
            if (closed_) {
                return;
            }
            closed_ = true;
        }

        if (sock() != UDT::INVALID_SOCK) {
            reactor().remove(this);
            resetSock();
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
        if (res != 0) {
            res |= UDT_EPOLL_ERR;
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
        if ((res = UDT::recv(sock(), req->first, req->last - req->first, 0)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot read UDT socket: " << UDT::getlasterror().getErrorMessage());

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

        if (!req->readAll) {
            cb = req->callback;
            total_read = req->total_read;
        }

        if (req->first >= req->last) {
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
        if ((res = UDT::send(sock(), req->first, req->last - req->first, 0)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot write UDT socket: " << UDT::getlasterror().getErrorMessage());

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

    void UDTConnection::handleClose()
    {
        // handleClose will not execute concurrently with read/write, can
        // do stuff without mutex here.

        for (std::list<WriteReq>::iterator it = writeQueue_.begin(); it != writeQueue_.end(); ++it) {
            it->callback(CUDTException::ENOCONN);
        }
        for (std::list<ReadReq>::iterator it = readQueue_.begin(); it != readQueue_.end(); ++it) {
            it->callback(CUDTException::ENOCONN, it->total_read);
        }

        writeQueue_.clear();
        readQueue_.clear();

        UDT::close(sock());
    }
}