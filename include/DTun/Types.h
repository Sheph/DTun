#ifndef _DTUN_TYPES_H_
#define _DTUN_TYPES_H_

#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <errno.h>
#define SYS_INVALID_SOCKET INVALID_SOCKET
#define SYS_SOCKET_ERROR SOCKET_ERROR
#else
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <net/if.h>

#define SYS_INVALID_SOCKET -1
#define SYS_SOCKET_ERROR -1
#endif

#ifndef __UDT_H__
typedef int SYSSOCKET;
#endif

namespace DTun
{
    typedef unsigned char UInt8;
    typedef signed char SInt8;
    typedef unsigned short UInt16;
    typedef signed short SInt16;
    typedef unsigned int UInt32;
    typedef signed int SInt32;
    typedef unsigned long long UInt64;
    typedef signed long long SInt64;
    typedef UInt8 Byte;
    typedef char Char;
}

#endif
