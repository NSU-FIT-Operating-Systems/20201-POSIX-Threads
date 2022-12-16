#ifndef HTTP_CPP_PROXY_SINGLE_THREAD_PROXY_H
#define HTTP_CPP_PROXY_SINGLE_THREAD_PROXY_H

#include <cstdlib>
#include <map>
#include <vector>
#include <set>

#include "proxy.h"
#include "select_data.h"
#include "io_operations.h"
#include "map_cache.h"

#include "httpparser/src/httpparser/httpresponseparser.h"
#include "httpparser/src/httpparser/response.h"

namespace single_thread_proxy {

    enum server_status {
        NOT_CONNECTED, CONNECTED
    };

    typedef struct resource_info {
        httpparser::HttpResponseParser::ParseResult status = httpparser::HttpResponseParser::ParsingIncompleted;
        io_operations::message *data = nullptr;
        bool free_message = true;
        // vector of socket descriptors of clients who wait for the resource
        std::set<int> subscribers = std::set<int>();

        resource_info() = default;

        ~resource_info() {
            if (free_message) delete data;
        }
    } resource_info;

    typedef struct server_info {
        std::vector<io_operations::message *> message_queue = std::vector<io_operations::message*>();
        server_status status = NOT_CONNECTED;
        std::string resource_name;

        server_info() = default;
        ~server_info() = default;
    } server_info;

    typedef struct client_info {
        std::vector<std::pair<std::string, io_operations::message *>> message_queue
                = std::vector<std::pair<std::string, io_operations::message *>>();

        client_info() = default;

        ~client_info() = default;
    } client_info;

    class http_proxy final : public proxy {
    public:

        http_proxy();

        ~http_proxy() final;

        void run(int port) final;

        void shutdown() final;

    private:

        int init_and_bind_proxy_socket(int port);

        int accept_new_client();

        int read_message_from(int fd);

        int read_client_request(int client_fd, io_operations::message *request_message);

        int read_server_response(int server_fd, io_operations::message *response_message);

        int write_message_to(int fd);

        int begin_connect_to_remote(const std::string &hostname, int port);

        int finish_connect_to_remote(int fd);

        int close_connection(int fd);

        void free_resources();

        size_t cache_size_bytes();

        bool is_running = false;
        int proxy_socket = 0;
        io_operations::select_data *selected;
        std::map<int, client_info> *clients;
        std::map<int, server_info> *servers;
        aiwannafly::cache<resource_info> *cache;
        std::map<std::string, struct hostent *> *DNS_map;
    };
}

#endif //HTTP_CPP_PROXY_SINGLE_THREAD_PROXY_H
