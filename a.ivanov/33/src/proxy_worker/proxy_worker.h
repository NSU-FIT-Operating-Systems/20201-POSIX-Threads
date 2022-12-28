#ifndef HTTP_CPP_PROXY_SINGLE_THREAD_PROXY_H
#define HTTP_CPP_PROXY_SINGLE_THREAD_PROXY_H

#include <cstdlib>
#include <map>
#include <set>
#include <vector>
#include <netdb.h>

#include "../proxy.h"
#include "../utils/select_data.h"
#include "../utils/io_operations.h"
#include "../cache/map_cache.h"
#include "../resource/resource_info.h"
#include "../runnable.h"
#include "peers.h"

#include "../../httpparser/src/httpparser/httpresponseparser.h"
#include "../../httpparser/src/httpparser/response.h"

namespace worker_thread_proxy {

    class ProxyWorker final : public Runnable {
    public:

        ProxyWorker(int signal_fd, aiwannafly::Cache<ResourceInfo> *cache);

        ~ProxyWorker() override;

        int run() override;

        [[nodiscard]] io::SelectData *getSelected() const;

    private:

        int readMessageFrom(int fd);

        int readClientRequest(int client_fd, io::Message *request_message);

        int readServerResponse(int server_fd, io::Message *new_part);

        void updateClientQueue(int fd, bool reset);

        int writeMessageTo(int fd);

        int beginConnectToServer(const std::string &hostname, int port);

        int finishConnectToServer(int fd);

        static void notifySubscribers(const std::string &res_name, ResourceInfo *resource) ;

        int closeConnection(int fd);

        void freeResources();

        int signal_fd = -1;
        io::SelectData *selected;
        std::map<int, ClientInfo> *clients;
        std::map<int, ServerInfo> *servers;
        aiwannafly::Cache<ResourceInfo> *cache;
        std::map<std::string, struct hostent *> *DNS_map;
    };
}

#endif //HTTP_CPP_PROXY_SINGLE_THREAD_PROXY_H
