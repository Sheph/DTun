#include "Logger.h"
#include "DTun/SignalHandler.h"
#include "DTun/SignalBlocker.h"
#include "DTun/SysManager.h"
#include "DTun/UTPManager.h"
#include "DTun/Utils.h"
#include "DTun/SAcceptor.h"
#include "DTun/SConnector.h"
#include "DTun/SConnection.h"
#include <boost/make_shared.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/program_options.hpp>
#include <log4cplus/configurator.h>
#include <iostream>

using namespace DCat;

static bool reverse = false;

boost::scoped_ptr<DTun::UTPManager> mgr;

boost::shared_ptr<DTun::SAcceptor> acceptor;

boost::shared_ptr<DTun::SConnector> connector;
boost::shared_ptr<DTun::SConnection> conn;
static char buff[4 * 1024];
static char buff2[128 * 1024];

static boost::chrono::steady_clock::time_point lastTs;
static int totalNumBytes;

static void onSend(int err)
{
    LOG4CPLUS_TRACE(logger(), "onSend(" << err << ")");

    if (err) {
        return;
    }

    totalNumBytes += sizeof(buff);

    boost::chrono::steady_clock::time_point now = boost::chrono::steady_clock::now();

    int us = boost::chrono::duration_cast<boost::chrono::microseconds>(
        now - lastTs).count();

    if (us >= 1000000) {
        float br = (float)totalNumBytes / (float)us;

        LOG4CPLUS_INFO(logger(), "br = " << int(br * 1000000 / 1000) << "kb");

        lastTs = now;
        totalNumBytes = 0;
    }

    conn->write(&buff[0], &buff[0] + sizeof(buff), &onSend);
}

void onRecv(int err, int numBytes)
{
    LOG4CPLUS_TRACE(logger(), "onRecv(" << err << ", " << numBytes << ")");

    if (err) {
        return;
    }

    totalNumBytes += numBytes;

    boost::chrono::steady_clock::time_point now = boost::chrono::steady_clock::now();

    int us = boost::chrono::duration_cast<boost::chrono::microseconds>(
        now - lastTs).count();

    if (us >= 1000000) {
        float br = (float)totalNumBytes / (float)us;

        LOG4CPLUS_INFO(logger(), "br = " << int(br * 1000000 / 1000) << "kb");

        lastTs = now;
        totalNumBytes = 0;
    }

    conn->read(&buff2[0], &buff2[0] + sizeof(buff2), &onRecv, false);
}

static void clientOnConnect(int err)
{
    LOG4CPLUS_INFO(logger(), "clientOnConnect(" << err << ")");

    boost::shared_ptr<DTun::SHandle> handle = connector->handle();

    connector->close();

    if (err) {
        handle->close();
    } else {
        lastTs = boost::chrono::steady_clock::now();

        conn = handle->createConnection();

        if (reverse) {
            conn->read(&buff2[0], &buff2[0] + sizeof(buff2), &onRecv, false);
        } else {
            for (int i = 0; i < 16; ++i) {
                conn->write(&buff[0], &buff[0] + sizeof(buff), &onSend);
            }
        }
    }
}

static void serverOnAccept(const boost::shared_ptr<DTun::SHandle>& handle)
{
    LOG4CPLUS_INFO(logger(), "serverOnAccept(" << handle << ")");

    conn = handle->createConnection();

    lastTs = boost::chrono::steady_clock::now();

    if (reverse) {
        for (int i = 0; i < 16; ++i) {
            conn->write(&buff[0], &buff[0] + sizeof(buff), &onSend);
        }
    } else {
        conn->read(&buff2[0], &buff2[0] + sizeof(buff2), &onRecv, false);
    }
}

static void runServer(int port)
{
    addrinfo hints;

    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::ostringstream os;
    os << port;

    addrinfo* res;

    if (::getaddrinfo(NULL, os.str().c_str(), &hints, &res) != 0) {
        LOG4CPLUS_ERROR(logger(), "Illegal port number or port is busy");
        return;
    }

    boost::shared_ptr<DTun::SHandle> serverHandle = mgr->createStreamSocket();
    if (!serverHandle) {
        freeaddrinfo(res);
        return;
    }

    if (!serverHandle->bind(res->ai_addr, res->ai_addrlen)) {
        freeaddrinfo(res);
        return;
    }

    freeaddrinfo(res);

    acceptor = serverHandle->createAcceptor();

    if (!acceptor->listen(10, &serverOnAccept)) {
       acceptor.reset();
       return;
    }

    LOG4CPLUS_INFO(logger(), "Server is ready at port " << port);
}

static void runClient(int localPort, const std::string& ip, int port)
{
    addrinfo hints;

    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::ostringstream os;
    os << localPort;

    addrinfo* res;

    if (::getaddrinfo(NULL, os.str().c_str(), &hints, &res) != 0) {
        LOG4CPLUS_ERROR(logger(), "Illegal port number or port is busy");
        return;
    }

    boost::shared_ptr<DTun::SHandle> clientHandle = mgr->createStreamSocket();
    if (!clientHandle) {
        freeaddrinfo(res);
        return;
    }

    if (!clientHandle->bind(res->ai_addr, res->ai_addrlen)) {
        freeaddrinfo(res);
        return;
    }

    freeaddrinfo(res);

    connector = clientHandle->createConnector();

    os.str("");
    os << port;

    if (!connector->connect(ip, os.str(), &clientOnConnect, DTun::SConnector::ModeNormal)) {
       connector.reset();
       return;
    }

    LOG4CPLUS_INFO(logger(), "Client bound at port " << localPort << " connects to " << ip << ":" << port);
}

static void signalHandler(int sig)
{
    LOG4CPLUS_INFO(logger(), "Signal " << sig << " received");
    mgr->reactor().stop();
}

int main(int argc, char* argv[])
{
    boost::program_options::variables_map vm;
    std::string logLevel = "TRACE";
    int listenPort = 0;
    int localPort = 0;
    std::string targetIp;
    int targetPort = 0;

    try {
        boost::program_options::options_description desc("Options");

        desc.add_options()
            ("log4cplus_level", boost::program_options::value<std::string>(&logLevel), "Log level")
            ("listenPort", boost::program_options::value<int>(&listenPort), "Listen port")
            ("localPort", boost::program_options::value<int>(&localPort), "Local port")
            ("targetIp", boost::program_options::value<std::string>(&targetIp), "Target IP")
            ("targetPort", boost::program_options::value<int>(&targetPort), "Target port")
            ("reverse", "Reverse");

        boost::program_options::store(boost::program_options::command_line_parser(
            argc, argv).options(desc).allow_unregistered().run(), vm);

        boost::program_options::notify(vm);

        reverse = (vm.count("reverse") > 0);
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
    props.setProperty("log4cplus.appender.console.layout.ConversionPattern", "%D{%m/%d/%y %H:%M:%S.%q} %-5p %c [%x] - %m%n");

    log4cplus::PropertyConfigurator propConf(props);
    propConf.configure();

    DTun::SignalBlocker signalBlocker(true);

    boost::scoped_ptr<DTun::SignalHandler> sigHandler(new DTun::SignalHandler(&signalHandler));

    boost::scoped_ptr<DTun::SysReactor> reactor;
    boost::scoped_ptr<DTun::SManager> innerMgr;

    reactor.reset(new DTun::SysReactor());
    innerMgr.reset(new DTun::SysManager(*reactor));
    mgr.reset(new DTun::UTPManager(*innerMgr));
    if (!mgr->start()) {
        return 1;
    }

    if (!mgr->reactor().start()) {
        return 1;
    }

    if (listenPort) {
        runServer(listenPort);
        mgr->reactor().run();
        acceptor.reset();
        conn.reset();
    } else {
        runClient(localPort, targetIp, targetPort);
        mgr->reactor().run();
        connector.reset();
        conn.reset();
    }

    LOG4CPLUS_INFO(logger(), "Done!");

    mgr.reset();

    reactor->processUpdates();

    return 0;
}
