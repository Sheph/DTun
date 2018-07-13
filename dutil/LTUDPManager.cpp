#include "DTun/LTUDPManager.h"

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

    bool LTUDPManager::start()
    {
    }

    boost::shared_ptr<SHandle> LTUDPManager::createStreamSocket()
    {
    }

    boost::shared_ptr<SHandle> LTUDPManager::createDatagramSocket()
    {
    }
}
