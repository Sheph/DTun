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
        LOG4CPLUS_TRACE(logger(), "LTUDPConnection::write");
    }

    void LTUDPConnection::read(char* first, char* last, const ReadCallback& callback, bool readAll)
    {
        LOG4CPLUS_TRACE(logger(), "LTUDPConnection::read");
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
}
