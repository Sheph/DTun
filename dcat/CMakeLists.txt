set(SOURCES
    main.cpp
    Logger.cpp
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_executable(dcat ${SOURCES} ${COMMON_HEADERS})

target_link_libraries(dcat dutil ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} rt dl)
