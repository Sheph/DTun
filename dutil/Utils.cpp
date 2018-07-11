#include "DTun/Utils.h"
#include "Logger.h"
#include <sstream>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef _WIN32
#define SYS_CLOSE_SOCKET(s) closesocket(s)
#else
#define SYS_CLOSE_SOCKET(s) ::close(s)
#endif

namespace DTun
{
    bool isDebuggerPresent()
    {
        char buf[1024];
        bool debuggerPresent = false;

        int statusFd = open("/proc/self/status", O_RDONLY);
        if (statusFd == -1) {
            return false;
        }

        ssize_t numRead = read(statusFd, buf, sizeof(buf) - 1);

        if (numRead > 0) {
            static const char tracerPidStr[] = "TracerPid:";
            char* tracerPid;

            buf[numRead] = 0;
            tracerPid = strstr(buf, tracerPidStr);
            if (tracerPid) {
                debuggerPresent = !!atoi(tracerPid + sizeof(tracerPidStr) - 1);
            }
        }

        return debuggerPresent;
    }

    std::string ipToString(UInt32 ipAddress)
    {
        ipAddress = ntohl(ipAddress);

        std::ostringstream os;

        for (int i = 0; i < 3; ++i) {
            os << ((ipAddress >> 24) & 0xFF) << ".";
            ipAddress <<= 8;
        }

        os << ((ipAddress >> 24) & 0xFF);

        return os.str();
    }

    bool stringToIp(const std::string& str, UInt32& ipAddress)
    {
        struct in_addr buff;

        if (::inet_pton(AF_INET, str.c_str(), &buff) <= 0) {
            return false;
        }

        ipAddress = buff.s_addr;

        return true;
    }

    std::string portToString(UInt16 port)
    {
        std::ostringstream os;

        os << ntohs(port);

        return os.str();
    }

    std::string ipPortToString(UInt32 ipAddress, UInt16 port)
    {
        std::ostringstream os;

        os << ipToString(ipAddress) << ":" << ntohs(port);

        return os.str();
    }

    void closeSysSocketChecked(SYSSOCKET sock)
    {
        if (SYS_CLOSE_SOCKET(sock) == -1) {
            LOG4CPLUS_ERROR(logger(), "Error closing sys socket: " << ::strerror(errno));
        }
    }

    void closeUDTSocketChecked(UDTSOCKET sock)
    {
        if (UDT::close(sock) == UDT::ERROR) {
            // FIXME: use ERROR after UDT implicit socket close fix
            LOG4CPLUS_TRACE(logger(), "Error closing UDT socket: " << UDT::getlasterror().getErrorMessage());
        }
    }
}
