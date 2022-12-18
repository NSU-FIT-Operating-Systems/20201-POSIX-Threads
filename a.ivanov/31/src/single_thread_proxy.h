#ifndef HTTP_CPP_PROXY_SINGLE_THREAD_PROXY_H
#define HTTP_CPP_PROXY_SINGLE_THREAD_PROXY_H

#include <cstdlib>
#include <map>
#include <set>
#include <vector>

#include "proxy.h"
#include "select_data.h"
#include "io_operations.h"
#include "map_cache.h"

#include "../httpparser/src/httpparser/httpresponseparser.h"
#include "../httpparser/src/httpparser/response.h"

namespace single_thread_proxy {

    typedef struct ResourceInfo {
        httpparser::HttpResponseParser::ParseResult status = httpparser::HttpResponseParser::ParsingIncompleted;
        std::vector<io::Message *> parts = std::vector<io::Message *>();
        io::Message *full_data = nullptr;
        size_t current_length = 0;
        size_t content_length = 0;
        bool free_messages = true;
        // vector of socket descriptors of clients who wait for the resource
        std::set<int> subscribers = std::set<int>();

        ResourceInfo() = default;

        ~ResourceInfo() {
            delete full_data;
            if (free_messages) {
                for (auto msg: parts) {
                    delete msg;
                }
            }
        }
    } ResourceInfo;

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

        ClientInfo() = default;
        ~ClientInfo() = default;
    } ClientInfo;

    class HttpProxy final : public proxy {
    public:

        explicit HttpProxy(bool print_allowed);

        ~HttpProxy() final;

        void run(int port) final;

        void shutdown() final;

    private:
        void log(const std::string &msg) const;

        void logError(const std::string &msg) const;

        void logErrorWithErrno(const std::string &msg) const;

        int initAndBindProxySocket(int port);

        int acceptNewClient();

        int readMessageFrom(int fd);

        int readClientRequest(int client_fd, io::Message *request_message);

        int readServerResponse(int server_fd, io::Message *new_part);

        int writeMessageTo(int fd);

        int beginConnectToServer(const std::string &hostname, int port);

        int finishConnectToServer(int fd);

        void sendLastResourcePart(const std::string &resource_name, ResourceInfo *resource);

        int closeConnection(int fd);

        void freeResources();

        size_t cacheSizeBytes();

        bool print_allowed = false;
        bool is_running = false;
        int proxy_socket = 0;
        io::SelectData *selected;
        std::map<int, ClientInfo> *clients;
        std::map<int, ServerInfo> *servers;
        aiwannafly::Cache<ResourceInfo> *cache;
        std::map<std::string, struct hostent *> *DNS_map;
    };
}

#endif //HTTP_CPP_PROXY_SINGLE_THREAD_PROXY_H
