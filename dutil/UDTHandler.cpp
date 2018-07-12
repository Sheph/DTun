#include "DTun/UDTHandler.h"
#include "Logger.h"

namespace DTun
{
    UDTHandler::UDTHandler(UDTReactor& reactor, const boost::shared_ptr<UDTHandle>& handle)
    : reactor_(reactor)
    , handle_(handle)
    , cookie_(0)
    {
    }

    UDTHandler::~UDTHandler()
    {
    }
}
