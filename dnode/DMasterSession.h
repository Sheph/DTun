#ifndef _DMASTERSESSION_H_
#define _DMASTERSESSION_H_

#include "DTun/DProtocol.h"
#include "DTun/SManager.h"
#include <boost/noncopyable.hpp>
#include <boost/function.hpp>
#include <map>
#include <vector>

namespace DNode
{
    class DMasterSession : boost::noncopyable
    {
    public:
        typedef boost::function<void (int, DTun::UInt16)> Callback;

        DMasterSession(DTun::SManager& mgr, const std::string& address, int port);
        ~DMasterSession();

        // 's' will be closed even in case of failure!
        bool startFast(SYSSOCKET s, DTun::UInt32 nodeId, const DTun::ConnId& connId,
            const Callback& callback);
        // 's' will be closed even in case of failure!
        bool startSymm(SYSSOCKET s, DTun::UInt32 nodeId, const DTun::ConnId& connId,
            const Callback& callback, bool dryRun = false);

        inline const boost::shared_ptr<DTun::SConnection>& conn() const { return conn_; }

    private:
        bool start(SYSSOCKET s, const Callback& callback);

        void onConnect(int err);
        void onSend(int err);
        void onRecv(int err, int numBytes);

        DTun::SManager& mgr_;
        std::string address_;
        int port_;
        std::vector<char> buff_;
        Callback callback_;
        boost::shared_ptr<DTun::SConnection> conn_;
        boost::shared_ptr<DTun::SConnector> connector_;
    };
}

#endif
