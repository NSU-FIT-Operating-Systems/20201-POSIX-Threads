#ifndef WORKER_THREADS_PROXY_PEERS_H
#define WORKER_THREADS_PROXY_PEERS_H

#include <vector>
#include "../utils/io_operations.h"
#include "../sync/threadsafe_list.h"

namespace worker_thread_proxy {

    typedef struct ServerInfo {
        std::vector<io::Message *> message_queue = std::vector<io::Message *>();
        bool connected = false;
        std::string res_name;

        ServerInfo() = default;

        ~ServerInfo() = default;
    } ServerInfo;

    typedef struct ClientInfo {
        std::vector<io::Message *> message_queue = std::vector<io::Message *>();
        std::string res_name;
        size_t recv_msg_count = 0;

        ClientInfo() = default;

        ~ClientInfo() = default;
    } ClientInfo;
}

#endif //WORKER_THREADS_PROXY_PEERS_H
