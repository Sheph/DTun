#include "Logger.h"

namespace DMaster
{
    log4cplus::Logger logger()
    {
        return log4cplus::Logger::getInstance("DMaster");
    }
}
