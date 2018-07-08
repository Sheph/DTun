#include "DMasterClient.h"
#include "Logger.h"
#include "DTun/SignalBlocker.h"
#include "DTun/UDTReactor.h"
#include "DTun/TCPReactor.h"
#include "DTun/Utils.h"
#include <boost/thread.hpp>
#include <boost/scoped_ptr.hpp>
#include <log4cplus/configurator.h>
#include <iostream>

extern "C" int tun2socks_main(int argc, char **argv, int is_debugged);

static void udtReactorThreadFn(DTun::UDTReactor& reactor)
{
    reactor.run();
}

static void tcpReactorThreadFn(DTun::TCPReactor& reactor)
{
    reactor.run();
}

int main(int argc, char* argv[])
{
    log4cplus::helpers::Properties props;

    props.setProperty("log4cplus.rootLogger", "TRACE, console");
    props.setProperty("log4cplus.appender.console", "log4cplus::ConsoleAppender");
    props.setProperty("log4cplus.appender.console.layout", "log4cplus::PatternLayout");
    props.setProperty("log4cplus.appender.console.layout.ConversionPattern", "%-5p %c [%x] - %m%n");

    log4cplus::PropertyConfigurator propConf(props);
    propConf.configure();

    int res = 0;

    bool isDebugged = DTun::isDebuggerPresent();

    {
        DTun::SignalBlocker signalBlocker(!isDebugged);

        UDT::startup();

        DTun::UDTReactor udtReactor;

        if (!udtReactor.start()) {
            UDT::cleanup();
            return 1;
        }

        DTun::TCPReactor tcpReactor;

        if (!tcpReactor.start()) {
            UDT::cleanup();
            return 1;
        }

        boost::scoped_ptr<boost::thread> udtReactorThread;
        boost::scoped_ptr<boost::thread> tcpReactorThread;

        {
            DNode::DMasterClient masterClient(udtReactor, tcpReactor, "127.0.0.1", 2345, 1);

            if (!masterClient.start()) {
                UDT::cleanup();
                return 1;
            }

            udtReactorThread.reset(new boost::thread(
                boost::bind(&udtReactorThreadFn, boost::ref(udtReactor))));
            tcpReactorThread.reset(new boost::thread(
                boost::bind(&tcpReactorThreadFn, boost::ref(tcpReactor))));

            LOG4CPLUS_INFO(DNode::logger(), "Started");

            signalBlocker.unblock();

            DNode::theMasterClient = &masterClient;

            res = tun2socks_main(argc, argv, isDebugged);

            DNode::theMasterClient = NULL;
        }

        udtReactor.stop();
        tcpReactor.stop();
        udtReactorThread->join();
        tcpReactorThread->join();
    }

    UDT::cleanup();

    LOG4CPLUS_INFO(DNode::logger(), "Done");

    return res;
}
