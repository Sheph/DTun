#include "Logger.h"

namespace DNode
{
    log4cplus::Logger logger()
    {
        return log4cplus::Logger::getInstance("DNode");
    }
}
