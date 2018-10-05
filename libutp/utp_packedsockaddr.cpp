// vim:set ts=4 sw=4 ai:

/*
 * Copyright (c) 2010-2013 BitTorrent, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "utp_types.h"
#include "utp_hash.h"
#include "utp_packedsockaddr.h"

#include "libutp_inet_ntop.h"

byte PackedSockAddr::get_family() const
{
    return AF_INET_UTP;
}

bool PackedSockAddr::operator==(const PackedSockAddr& rhs) const
{
    if (&rhs == this)
        return true;
    if (memcmp(&_port[0], &rhs._port[0], sizeof(in_port_utp)) != 0)
        return false;
    return memcmp(_sin6, rhs._sin6, sizeof(_sin6)) == 0;
}

bool PackedSockAddr::operator!=(const PackedSockAddr& rhs) const
{
    return !(*this == rhs);
}

uint32 PackedSockAddr::compute_hash() const {
    return utp_hash_mem(&_in, sizeof(_in)) ^ utp_hash_mem(&_port, sizeof(in_port_utp));
}

void PackedSockAddr::set(const SOCKADDR_STORAGE* sa, socklen_t len)
{
    assert(sa->ss_family == AF_INET_UTP);

    assert(len >= sizeof(sockaddr_in_utp));
    const sockaddr_in_utp *sin = (sockaddr_in_utp*)sa;
    _sin6w[0] = 0;
    _sin6w[1] = 0;
    _sin6w[2] = 0;
    _sin6w[3] = 0;
    _sin6w[4] = 0;
    _sin6w[5] = 0xffff;
    _sin4 = sin->sin_addr.s_addr;
    memcpy(&_port[0], &sin->sin_port[0], sizeof(in_port_utp));
}

PackedSockAddr::PackedSockAddr(const SOCKADDR_STORAGE* sa, socklen_t len)
{
    set(sa, len);
}

PackedSockAddr::PackedSockAddr(void)
{
    struct sockaddr_in_utp addr;
    socklen_t len = sizeof(addr);
    memset(&addr, 0, len);
    addr.sin_family = AF_INET_UTP;
    set((const SOCKADDR_STORAGE*)&addr, len);
}

struct sockaddr_in_utp PackedSockAddr::get_sockaddr_storage(socklen_t *len = NULL) const
{
    struct sockaddr_in_utp sin;

    if (len) *len = sizeof(sin);
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET_UTP;
    memcpy(&sin.sin_port[0], &_port[0], sizeof(in_port_utp));
    sin.sin_addr.s_addr = _sin4;

    return sin;
}

// #define addrfmt(x, s) x.fmt(s, sizeof(s))
cstr PackedSockAddr::fmt(str s, size_t len) const
{
    memset(s, 0, len);
    str i;
    INET_NTOP(AF_INET, (uint32*)&_sin4, s, len);
    i = s;
    while (*++i) {}
    snprintf(i, len - (i-s), ":%02x%02x", (int)_port[0], (int)_port[1]);
    return s;
}
