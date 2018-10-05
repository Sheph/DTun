set(UTP_SOURCES
    utp_api.cpp
    utp_callbacks.cpp
    utp_hash.cpp
    utp_internal.cpp
    utp_packedsockaddr.cpp
    utp_utils.cpp
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_definitions(-DPOSIX)

add_library(utp STATIC ${UTP_SOURCES})
