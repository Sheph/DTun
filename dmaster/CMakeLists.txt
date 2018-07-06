set(SOURCES
    main.cpp
    Logger.cpp
    Server.cpp
    Session.cpp
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_executable(dmaster ${SOURCES} ${COMMON_HEADERS})

target_link_libraries(dmaster dutil udt ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} rt dl)
