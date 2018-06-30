set(SOURCES
    main.cpp
    Logger.cpp
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_executable(dnode ${SOURCES})

target_link_libraries(dnode dutil ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} rt dl)
