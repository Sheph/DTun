#include "DMasterClient.h"
#include "Logger.h"
#include "DTun/SignalBlocker.h"
#include "DTun/UDTReactor.h"
#include "DTun/SysReactor.h"
#include "DTun/UDTManager.h"
#include "DTun/SysManager.h"
#include "DTun/Utils.h"
#include "DTun/StreamAppConfig.h"
#include <boost/thread.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/program_options.hpp>
#include <boost/make_shared.hpp>
#include <log4cplus/configurator.h>
#include <iostream>

extern "C" int tun2socks_main(int argc, char **argv, int is_debugged, void (*stats_handler)(void*));

static void udtReactorThreadFn(DTun::UDTReactor& reactor)
{
    reactor.run();
}

static void sysReactorThreadFn(DTun::SysReactor& reactor)
{
    reactor.run();
}

extern "C" void theStatsHandler(void*)
{
    DNode::theMasterClient->dump();
}

extern "C" int tun2socks_needs_proxy(uint32_t ip)
{
    DTun::UInt32 tmp = 0;
    return DNode::theMasterClient->getDstNodeId(ip, tmp);
}

namespace DNode
{
    DTun::SManager* theRemoteMgr = NULL;
}

int main(int argc, char* argv[])
{
    boost::program_options::variables_map vm;
    std::string logLevel = "TRACE";
    std::string appConfigFile = "config.ini";

    try {
        boost::program_options::options_description desc("Options");

        desc.add_options()
            ("log4cplus_level", boost::program_options::value<std::string>(&logLevel), "Log level")
            ("app_config", boost::program_options::value<std::string>(&appConfigFile), "App config");

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

    props.setProperty("log4cplus.rootLogger", logLevel + ", console");
    props.setProperty("log4cplus.appender.console", "log4cplus::ConsoleAppender");
    props.setProperty("log4cplus.appender.console.layout", "log4cplus::PatternLayout");
    props.setProperty("log4cplus.appender.console.layout.ConversionPattern", "%-5p %c [%x] - %m%n");

    log4cplus::PropertyConfigurator propConf(props);
    propConf.configure();

    boost::shared_ptr<DTun::StreamAppConfig> appConfig =
        boost::make_shared<DTun::StreamAppConfig>();

    std::ifstream is(appConfigFile.c_str());

    if (is) {
        if (!appConfig->load(is)) {
            LOG4CPLUS_WARN(DNode::logger(), "Cannot parse app config file " << appConfigFile);
        }

        is.close();
    } else {
        LOG4CPLUS_WARN(DNode::logger(), "App config file " << appConfigFile << " not found");
    }

    int res = 0;

    bool isDebugged = DTun::isDebuggerPresent();

    {
        DTun::SignalBlocker signalBlocker(true);

        UDT::startup();

        DTun::UDTReactor udtReactor;

        if (!udtReactor.start()) {
            UDT::cleanup();
            return 1;
        }

        DTun::SysReactor sysReactor;

        if (!sysReactor.start()) {
            UDT::cleanup();
            return 1;
        }

        boost::scoped_ptr<boost::thread> udtReactorThread;
        boost::scoped_ptr<boost::thread> sysReactorThread;

        {
            DTun::UDTManager udtManager(udtReactor);
            DTun::SysManager sysManager(sysReactor);

            DNode::DMasterClient masterClient(udtManager, sysManager, appConfig);

            if (!masterClient.start()) {
                UDT::cleanup();
                return 1;
            }

            udtReactorThread.reset(new boost::thread(
                boost::bind(&udtReactorThreadFn, boost::ref(udtReactor))));
            sysReactorThread.reset(new boost::thread(
                boost::bind(&sysReactorThreadFn, boost::ref(sysReactor))));

            LOG4CPLUS_INFO(DNode::logger(), "Started");

            signalBlocker.unblock();

            DNode::theMasterClient = &masterClient;
            DNode::theRemoteMgr = &udtManager;

            res = tun2socks_main(argc, argv, isDebugged, &theStatsHandler);

            DNode::theMasterClient = NULL;
            DNode::theRemoteMgr = NULL;
        }

        udtReactor.stop();
        sysReactor.stop();
        udtReactorThread->join();
        sysReactorThread->join();
    }

    UDT::cleanup();

    LOG4CPLUS_INFO(DNode::logger(), "Done");

    return res;
}
