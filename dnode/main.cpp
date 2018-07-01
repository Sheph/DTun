#include "Logger.h"
#include "DTun/SignalHandler.h"
#include "DTun/SignalBlocker.h"
#include <log4cplus/configurator.h>
#include <iostream>

extern "C" int tun2socks_main(int argc, char **argv);

static void signalHandler(int sig)
{
    LOG4CPLUS_INFO(DNode::logger(), "Signal " << sig << " received");
}

int main(int argc, char* argv[])
{
    log4cplus::helpers::Properties props;

    props.setProperty("log4cplus.rootLogger", "TRACE, console");
    props.setProperty("log4cplus.appender.console", "log4cplus::ConsoleAppender");
    props.setProperty("log4cplus.appender.console.layout", "log4cplus::PatternLayout");
    props.setProperty("log4cplus.appender.console.layout.ConversionPattern", "%-5p %c - %m%n");

    log4cplus::PropertyConfigurator propConf(props);
    propConf.configure();

    DTun::SignalBlocker signalBlocker;

    DTun::SignalHandler sigHandler(&signalHandler);

    LOG4CPLUS_INFO(DNode::logger(), "Started");

    int res = tun2socks_main(argc, argv);

    LOG4CPLUS_INFO(DNode::logger(), "Done");

    return res;
}
