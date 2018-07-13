#include "DTun/LTUDPManager.h"
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

    boost::shared_ptr<SHandle> LTUDPManager::createStreamSocket()
    {
        return boost::shared_ptr<SHandle>();
    }

    boost::shared_ptr<SHandle> LTUDPManager::createDatagramSocket()
    {
        return boost::shared_ptr<SHandle>();
    }

    void LTUDPManager::onTcpTimeout()
    {
        LOG4CPLUS_TRACE(logger(), "onTcpTimeout()");

        innerMgr_.reactor().post(
            watch_->wrap(boost::bind(&LTUDPManager::onTcpTimeout, this)), TCP_TMR_INTERVAL);
    }
}
