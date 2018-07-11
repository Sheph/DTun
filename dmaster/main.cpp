#include "Server.h"
#include "Logger.h"
#include "DTun/SignalHandler.h"
#include "DTun/SignalBlocker.h"
#include "DTun/Utils.h"
#include <boost/make_shared.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/program_options.hpp>
#include <log4cplus/configurator.h>
#include <iostream>

using namespace DMaster;

boost::shared_ptr<Server> server;

static void signalHandler(int sig)
{
    LOG4CPLUS_INFO(logger(), "Signal " << sig << " received");
    boost::shared_ptr<Server> tmp = server;
    if (tmp) {
        tmp->stop();
        server.reset();
    }
}

int main(int argc, char* argv[])
{
    boost::program_options::variables_map vm;
    std::string logLevel = "TRACE";
    int port = 2345;

    try {
        boost::program_options::options_description desc("Options");

        desc.add_options()
            ("log4cplus_level", boost::program_options::value<std::string>(&logLevel), "Log level")
            ("port", boost::program_options::value<int>(&port), "Port");

        boost::program_options::store(boost::program_options::command_line_parser(
            argc, argv).options(desc).allow_unregistered().run(), vm);

        boost::program_options::notify(vm);
    } catch (const boost::program_options::error& e) {
        std::cerr << "Invalid command line arguments: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    log4cplus::helpers::Properties props;

    props.setProperty("log4cplus.rootLogger", logLevel +  ", console");
    props.setProperty("log4cplus.appender.console", "log4cplus::ConsoleAppender");
    props.setProperty("log4cplus.appender.console.layout", "log4cplus::PatternLayout");
    props.setProperty("log4cplus.appender.console.layout.ConversionPattern", "%-5p %c [%x] - %m%n");

    log4cplus::PropertyConfigurator propConf(props);
    propConf.configure();

    bool isDebugged = DTun::isDebuggerPresent();

    DTun::SignalBlocker signalBlocker(!isDebugged);

    boost::scoped_ptr<DTun::SignalHandler> sigHandler;

    if (!isDebugged) {
        sigHandler.reset(new DTun::SignalHandler(&signalHandler));
    }

    UDT::startup();

    boost::shared_ptr<Server> server_tmp = boost::make_shared<Server>(port);

    if (!server_tmp->start()) {
        UDT::cleanup();
        return 1;
    }

    server = server_tmp;

    server_tmp->run();

    server.reset();
    server_tmp.reset();

    UDT::cleanup();

    LOG4CPLUS_INFO(logger(), "Done!");

    return 0;
}
