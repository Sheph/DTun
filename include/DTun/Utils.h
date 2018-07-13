#ifndef _DTUN_UTILS_H_
#define _DTUN_UTILS_H_

#include "DTun/Types.h"

namespace DTun
{
    DTUN_API bool isDebuggerPresent();

    DTUN_API std::string ipToString(UInt32 ipAddress);

    DTUN_API bool stringToIp(const std::string& str, UInt32& ipAddress);

    DTUN_API std::string portToString(UInt16 port);

    DTUN_API std::string ipPortToString(UInt32 ipAddress, UInt16 port);

    DTUN_API void closeSysSocketChecked(SYSSOCKET sock);
    DTUN_API void closeUDTSocketChecked(int sock);
}

#endif
