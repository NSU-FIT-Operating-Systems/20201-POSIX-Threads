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
#include "../resource/resource.h"
#include "../runnable.h"

#include "../../httpparser/src/httpparser/httpresponseparser.h"
#include "../../httpparser/src/httpparser/response.h"

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

    class ProxyWorker final : public Runnable {
    public:

        ProxyWorker(int notice_fd, int signal_fd, aiwannafly::Cache<Resource> *cache);

        ~ProxyWorker() override;

        int run() override;

        [[nodiscard]] int getNoticeFd() const;

    private:

        int readMessageFrom(int fd);

        int readResourceNotification(int notice_fd);

        int readClientRequest(int client_fd, io::Message *request_message);

        int readServerResponse(int server_fd, io::Message *new_part);

        int writeMessageTo(int fd);

        int beginConnectToServer(const std::string &hostname, int port);

        int finishConnectToServer(int fd);

        static int notifySubscribers(Resource *resource);

        int closeConnection(int fd);

        void freeResources();

        int notice_fd = -1;
        int signal_fd = -1;
        io::SelectData *selected;
        std::map<int, ClientInfo> *clients;
        std::map<int, ServerInfo> *servers;
        std::map<int, std::string> *resource_names;

        aiwannafly::Cache<Resource> *cache;
        std::map<std::string, struct hostent *> *DNS_map;
    };
}

#endif //HTTP_CPP_PROXY_SINGLE_THREAD_PROXY_H
