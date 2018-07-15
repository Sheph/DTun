#include "DTun/LTUDPConnection.h"
#include "DTun/LTUDPManager.h"
#include "Logger.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

namespace DTun
{
    LTUDPConnection::LTUDPConnection(const boost::shared_ptr<LTUDPHandle>& handle)
    : handle_(handle)
    , watch_(boost::make_shared<OpWatch>(boost::ref(handle->reactor())))
    {
        handle_->impl()->setWriteCallback(
            watch_->wrap<int, int>(boost::bind(&LTUDPConnection::onHandleWrite, this, _1, _2)));
        handle_->impl()->setReadCallback(
            watch_->wrap(boost::bind(&LTUDPConnection::onHandleRead, this)));
    }

    LTUDPConnection::~LTUDPConnection()
    {
        close();
    }

    void LTUDPConnection::close()
    {
        if (watch_->close()) {
            handle_->close();
        }
    }

    void LTUDPConnection::write(const char* first, const char* last, const WriteCallback& callback)
    {
        handle_->reactor().post(watch_->wrap(
            boost::bind(&LTUDPConnection::onWrite, this, first, last, callback)));
    }

    void LTUDPConnection::read(char* first, char* last, const ReadCallback& callback, bool readAll)
    {
        handle_->reactor().post(watch_->wrap(
            boost::bind(&LTUDPConnection::onRead, this, first, last, callback, readAll)));
    }

    void LTUDPConnection::writeTo(const char* first, const char* last, UInt32 destIp, UInt16 destPort, const WriteCallback& callback)
    {
        assert(false);
        LOG4CPLUS_FATAL(logger(), "writeTo not supported!");
    }

    void LTUDPConnection::readFrom(char* first, char* last, const ReadFromCallback& callback)
    {
        assert(false);
        LOG4CPLUS_FATAL(logger(), "readFrom not supported!");
    }

    void LTUDPConnection::onWrite(const char* first, const char* last, const WriteCallback& callback)
    {
        WriteReq req;

        req.first = first;
        req.last = last;
        req.callback = callback;

        writeQueue_.push_back(req);
        writeOutQueue_.push_back(std::make_pair(first, last));

        if (writeQueue_.size() == 1) {
            onHandleWrite(0, 0);
        }
    }

    void LTUDPConnection::onRead(char* first, char* last, const ReadCallback& callback, bool readAll)
    {
        ReadReq req;

        req.first = first;
        req.last = last;
        req.total_read = 0;
        req.callback = callback;
        req.readAll = readAll;

        readQueue_.push_back(req);

        if (readQueue_.size() == 1) {
            onHandleRead();
        }
    }

    void LTUDPConnection::onHandleWrite(int err, int numBytes)
    {
        // capture 'handle_', always check it first because
        // 'cb' might have deleted 'this'.
        boost::shared_ptr<LTUDPHandle> handle = handle_;

        if (err) {
            writeOutQueue_.clear();
        }

        while ((numBytes > 0) && handle->impl()) {
            assert(!writeQueue_.empty());

            WriteReq* req = &writeQueue_.front();

            int numWritten = std::min((int)(req->last - req->first), numBytes);

            req->first += numWritten;
            numBytes -= numWritten;

            if (req->first >= req->last) {
                WriteCallback cb = req->callback;
                writeQueue_.pop_front();
                cb((numBytes == 0) ? err : 0);
            }
        }

        while (err && handle->impl() && !writeQueue_.empty()) {
            WriteReq* req = &writeQueue_.front();
            WriteCallback cb = req->callback;
            writeQueue_.pop_front();
            cb(err);
        }

        if (!handle->impl() || err) {
            return;
        }

        for (std::list<WriteOutReq>::iterator it = writeOutQueue_.begin(); it != writeOutQueue_.end();) {
            int numWritten = 0;

            int err = handle->impl()->write(it->first, it->second, numWritten);

            if (err) {
                writeOutQueue_.clear();
                while (handle->impl() && !writeQueue_.empty()) {
                    WriteReq* req = &writeQueue_.front();
                    WriteCallback cb = req->callback;
                    writeQueue_.pop_front();
                    cb(err);
                }
                break;
            }

            if (numWritten == 0) {
                break;
            }

            it->first += numWritten;

            if (it->first >= it->second) {
                writeOutQueue_.erase(it++);
            } else {
                ++it;
            }
        }
    }

    void LTUDPConnection::onHandleRead()
    {
        // capture 'handle_', always check it first because
        // 'cb' might have deleted 'this'.
        boost::shared_ptr<LTUDPHandle> handle = handle_;

        while (handle->impl() && !readQueue_.empty()) {
            ReadReq* req = &readQueue_.front();

            int numBytes = 0;

            int err = handle->impl()->read(req->first, req->last, numBytes);

            if ((numBytes <= 0) && (err == 0)) {
                break;
            }

            req->first += numBytes;
            req->total_read += numBytes;

            if (!req->readAll || (req->first >= req->last) || err) {
                ReadCallback cb = req->callback;
                int total_read = req->total_read;
                readQueue_.pop_front();
                cb(err, total_read);
            }
        }
    }
}
