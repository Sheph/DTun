#include "DTun/SysHandler.h"
#include "Logger.h"

namespace DTun
{
    SysHandler::SysHandler(SysReactor& reactor, const boost::shared_ptr<SysHandle>& handle)
    : reactor_(reactor)
    , handle_(handle)
    , cookie_(0)
    {
    }

    SysHandler::~SysHandler()
    {
    }
}
