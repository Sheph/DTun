set(SOURCES
    main.cpp
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_executable(UDTNode ${SOURCES})

target_link_libraries(UDTNode ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} rt dl)
