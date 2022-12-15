#ifndef HTTP_CPP_PROXY_SINGLE_THREAD_PROXY_H
#define HTTP_CPP_PROXY_SINGLE_THREAD_PROXY_H

#include <cstdlib>
#include <map>
#include <vector>
#include <set>
#include "proxy.h"
#include "select_data.h"
#include "io_operations.h"

#include "httpparser/src/httpparser/httpresponseparser.h"
#include "httpparser/src/httpparser/response.h"

namespace single_thread_proxy {

    enum server_status {
        NOT_CONNECTED, CONNECTED
    };

    typedef struct resource_info {
        httpparser::HttpResponseParser::ParseResult status = httpparser::HttpResponseParser::ParsingIncompleted;
        io_operations::message *data = nullptr;
        // vector of socket descriptors of clients who wait for the resource
        std::set<int> subscribers = std::set<int>();

        resource_info() = default;

        ~resource_info() = default;
    } resource_info;

    typedef struct server_info {
        std::vector<io_operations::message *> message_queue = std::vector<io_operations::message *>();
        server_status status = NOT_CONNECTED;
        resource_info resource = resource_info();

        server_info() = default;

        ~server_info() = default;
    } server_info;

    typedef struct client_info {
        std::vector<io_operations::message *> message_queue = std::vector<io_operations::message *>();

        client_info() = default;

        ~client_info() = default;
    } client_info;

    class http_proxy final : public proxy {
    public:
        static const int MAX_CLIENTS_COUNT = 500;
        static const int MAX_WAIT_TIME_SECS = 10 * 60;

        http_proxy();

        ~http_proxy() final;

        void run(int port) final;

        void shutdown() final;

    private:
        static void log(const std::string &msg);

        static void log_error(const std::string &msg);

        static void log_error_with_errno(const std::string &msg);

        int init_and_bind_proxy_socket(int port);

        int accept_new_client();

        int handle_read_message(int fd);

        int handle_client_request(int client_fd, io_operations::message *request_message);

        int handle_server_response(int server_fd, io_operations::message *response_message);

        int handle_write_message(int fd);

        int begin_connect_to_remote(const std::string &hostname, int port);

        int finish_connect_to_remote(int fd);

        int close_connection(int fd);

        void close_all_connections();

        bool is_running = false;
        int proxy_socket = 0;
        io_operations::select_data *selected;
        std::map<int, client_info> *clients;
        std::map<int, server_info> *servers;
        std::map<std::string, int> *resources;
    };
}

#endif //HTTP_CPP_PROXY_SINGLE_THREAD_PROXY_H
