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
        std::vector<io::message *> parts = std::vector<io::message *>();
        io::message *full_data = nullptr;
        size_t current_length = 0;
        size_t content_length = 0;
        bool free_messages = true;
        // vector of socket descriptors of clients who wait for the resource
        std::set<int> subscribers = std::set<int>();

        resource_info() = default;

        ~resource_info() {
            if (free_messages) {
                for (auto msg: parts) {
                    delete msg;
                }
            }
        }
    } resource_info;

    typedef struct server_info {
        std::vector<io::message *> message_queue = std::vector<io::message *>();
        server_status status = NOT_CONNECTED;
        std::string res_name = "nothing";

        server_info() = default;

        ~server_info() = default;
    } server_info;

    typedef struct client_info {
        size_t received_bytes = 0;
        std::vector<std::pair<std::string, io::message *>> message_queue
                = std::vector<std::pair<std::string, io::message *>>();

        client_info() = default;

        ~client_info() = default;
    } client_info;

    class http_proxy final : public proxy {
    public:

        explicit http_proxy(bool print_allowed);

        ~http_proxy() final;

        void run(int port) final;

        void shutdown() final;

    private:
        void log(const std::string &msg) const;

        void log_error(const std::string &msg) const;

        void log_error_with_errno(const std::string &msg) const;

        int init_and_bind_proxy_socket(int port);

        int accept_new_client();

        int read_message_from(int fd);

        int read_client_request(int client_fd, io::message *request_message);

        int read_server_response(int server_fd, io::message *new_part);

        int write_message_to(int fd);

        int begin_connect_to_remote(const std::string &hostname, int port);

        int finish_connect_to_remote(int fd);

        void send_resource_part(const std::string &resource_name, resource_info *resource);

        int close_connection(int fd);

        void free_resources();

        size_t cache_size_bytes();

        bool print_allowed = false;
        bool is_running = false;
        int proxy_socket = 0;
        io::select_data *selected;
        std::map<int, client_info> *clients;
        std::map<int, server_info> *servers;
        aiwannafly::cache<resource_info> *cache;
        std::map<std::string, struct hostent *> *DNS_map;
    };
}

#endif //HTTP_CPP_PROXY_SINGLE_THREAD_PROXY_H
