#include "DTun/LTUDPManager.h"
#include "DTun/LTUDPHandle.h"
#include "Logger.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>
#include <lwip/priv/tcp_priv.h>

namespace DTun
{
    LTUDPManager::LTUDPManager(SManager& mgr)
    : innerMgr_(mgr)
    , numAliveHandles_(0)
    {
    }

    LTUDPManager::~LTUDPManager()
    {
        if (!watch_) {
            return;
        }

        watch_->close();
        assert(numAliveHandles_ == 0);
    }

    SReactor& LTUDPManager::reactor()
    {
        return innerMgr_.reactor();
    }

    bool LTUDPManager::start()
    {
        watch_ = boost::make_shared<OpWatch>();

        innerMgr_.reactor().post(
            watch_->wrap(boost::bind(&LTUDPManager::onTcpTimeout, this)), TCP_TMR_INTERVAL);

        return true;
    }

    void LTUDPManager::addToKill(const boost::shared_ptr<LTUDPHandleImpl>& handle)
    {
        boost::mutex::scoped_lock lock(m_);
        --numAliveHandles_;
    }

    boost::shared_ptr<SHandle> LTUDPManager::createStreamSocket()
    {
        boost::shared_ptr<SHandle> handle = innerMgr_.createDatagramSocket();
        if (!handle) {
            return handle;
        }

        boost::mutex::scoped_lock lock(m_);
        ++numAliveHandles_;
        return boost::make_shared<LTUDPHandle>(boost::ref(*this), handle->createConnection());
    }

    boost::shared_ptr<SHandle> LTUDPManager::createDatagramSocket()
    {
        assert(false);
        return boost::shared_ptr<SHandle>();
    }

    void LTUDPManager::onTcpTimeout()
    {
        LOG4CPLUS_TRACE(logger(), "onTcpTimeout()");

        innerMgr_.reactor().post(
            watch_->wrap(boost::bind(&LTUDPManager::onTcpTimeout, this)), TCP_TMR_INTERVAL);
    }
}
