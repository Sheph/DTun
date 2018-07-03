set(UDT_SOURCES
    api.cpp
    buffer.cpp
    cache.cpp
    ccc.cpp
    channel.cpp
    common.cpp
    core.cpp
    epoll.cpp
    list.cpp
    md5.cpp
    packet.cpp
    queue.cpp
    window.cpp
)

add_library(udt STATIC ${UDT_SOURCES})
