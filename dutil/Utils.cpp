#include "DTun/Utils.h"
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

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
}
