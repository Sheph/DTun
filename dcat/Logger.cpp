#include "Logger.h"

namespace DCat
{
	log4cplus::Logger logger()
	{
		return log4cplus::Logger::getInstance("DCat");
	}
}
