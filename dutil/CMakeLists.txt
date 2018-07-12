set(SOURCES
    Logger.cpp
    SignalBlocker.cpp
    SignalHandler.cpp
    StreamAppConfig.cpp
    SysConnection.cpp
    SysConnector.cpp
    SysHandle.cpp
    SysHandler.cpp
    SysManager.cpp
    SysReactor.cpp
    UDTAcceptor.cpp
    UDTConnection.cpp
    UDTConnector.cpp
    UDTHandle.cpp
    UDTHandler.cpp
    UDTManager.cpp
    UDTReactor.cpp
    Utils.cpp
)

add_library(dutil STATIC ${SOURCES})

target_link_libraries(dutil ${LOG4CPLUS_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY} ${Boost_CHRONO_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} rt)
