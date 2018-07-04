#ifndef _CONNECTION_H_
#define _CONNECTION_H_

#include <boost/noncopyable.hpp>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <list>
#include "udt.h"

namespace DMaster
{
    class Connection : boost::noncopyable
    {
    public:
        typedef boost::function<void (int)> SendCallback;
        typedef boost::function<void (int, int)> RecvCallback;

        explicit Connection(UDTSOCKET s);
        ~Connection();

        inline UDTSOCKET sock() const { return s_;}

        void send(const char* first, const char* last, const SendCallback& callback);

        void recv(char* first, char* last, const RecvCallback& callback);

        int getPollEvents(bool& changed);

        bool handleRead();
        bool handleSend();

    private:
        struct SendReq
        {
            std::vector<char> buff;
            size_t offset;
            SendCallback callback;
        };

        struct RecvReq
        {
            char* first;
            char* last;
            RecvCallback callback;
        };

        UDTSOCKET s_;
        std::list<SendReq> sendQueue_;
        std::list<RecvReq> recvQueue_;
    };
}

#endif
