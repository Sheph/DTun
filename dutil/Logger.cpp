#include "Logger.h"

namespace DTun
{
    log4cplus::Logger logger()
    {
        return log4cplus::Logger::getInstance("DTun");
    }
}
