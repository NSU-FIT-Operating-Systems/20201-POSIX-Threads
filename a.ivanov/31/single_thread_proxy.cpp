#include <unistd.h>
#include <csignal>
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <cstring>
#include <netdb.h>
#include <fcntl.h>
#include <cassert>

#include "httpparser/src/httpparser/request.h"
#include "httpparser/src/httpparser/httprequestparser.h"

#include "single_thread_proxy.h"
#include "status_code.h"
#include "socket_operations.h"

#define TERMINATE_CMD "stop"

namespace single_thread_proxy {
    static const int MAX_LISTEN_QUEUE_SIZE = 500;
    static const int HTTP_PORT = 80;
    int signal_pipe[2];

    void http_proxy::log(const std::string &msg) const {
        if (!print_allowed) return;
        std::cout << "[PROXY] " << msg << std::endl;
    }

    void http_proxy::log_error(const std::string &msg) const {
        if (!print_allowed) return;
        std::cerr << "[PROXY] " << msg << std::endl;
    }

    void http_proxy::log_error_with_errno(const std::string &msg) const {
        if (!print_allowed) return;
        perror(("[PROXY] " + msg).data());
    }

    void send_terminate(__attribute__((unused)) int sig) {
        auto *terminate = new io_operations::message();
        char *cmd = (char *) malloc(strlen(TERMINATE_CMD) + 1);
        if (cmd == nullptr) {
            return;
        }
        strcpy(cmd, TERMINATE_CMD);
        terminate->data = cmd;
        terminate->len = strlen(TERMINATE_CMD);
        io_operations::write_all(signal_pipe[io_operations::WRITE_PIPE_END], terminate);
        delete terminate;
    }

    int init_signal_handlers() {
        int return_value = pipe(signal_pipe);
        if (return_value == status_code::FAIL) {
            return status_code::FAIL;
        }
        signal(SIGINT, send_terminate);
        signal(SIGTERM, send_terminate);
        return status_code::SUCCESS;
    }

    void http_proxy::run(int port) {
        is_running = true;
        int ret_val = init_and_bind_proxy_socket(port);
        if (ret_val == status_code::FAIL) {
            log_error("Could not init and bind proxy socket");
            return;
        }
        ret_val = init_signal_handlers();
        if (ret_val == status_code::FAIL) {
            log_error_with_errno("Could not init signal handlers");
            return;
        }
        ret_val = listen(proxy_socket, MAX_LISTEN_QUEUE_SIZE);
        if (ret_val == status_code::FAIL) {
            log_error_with_errno("Error in listen");
            ret_val = close(proxy_socket);
            if (ret_val == status_code::FAIL) {
                log_error_with_errno("Error in close");
            }
        }
        selected->add_fd(proxy_socket, io_operations::select_data::READ);
        selected->add_fd(signal_pipe[io_operations::READ_PIPE_END], io_operations::select_data::READ);
        log("Running on " + std::to_string(port));
        fd_set constant_read_set;
        fd_set constant_write_set;
        while (true) {
            log("Waiting on select");
            memcpy(&constant_read_set, selected->get_read_set(), sizeof(*selected->get_read_set()));
            memcpy(&constant_write_set, selected->get_write_set(), sizeof(*selected->get_write_set()));
            ret_val = select(selected->get_max_fd() + 1, &constant_read_set, &constant_write_set,
                             nullptr, nullptr);
            if (ret_val == status_code::FAIL || ret_val == status_code::TIMEOUT) {
                if (errno != EINTR && ret_val == status_code::FAIL) {
                    log_error_with_errno("Error in select");
                    free_resources();
                    return;
                }
                if (ret_val == status_code::TIMEOUT) {
                    log("Select timed out. End program.");
                    free_resources();
                    return;
                }
            }
            int desc_ready = ret_val;
            for (int fd = 0; fd <= selected->get_max_fd() && desc_ready > 0; ++fd) {
                if (FD_ISSET(fd, &constant_read_set)) {
                    desc_ready -= 1;
                    if (fd == proxy_socket) {
                        log("Handle new connection");
                        ret_val = accept_new_client();
                        if (ret_val == status_code::FAIL) {
                            log_error_with_errno("Failed to accept new connection");
                            break;
                        }
                    } else {
                        log("Handle new message");
                        ret_val = read_message_from(fd);
                        if (ret_val == status_code::TERMINATE) {
                            free_resources();
                            return;
                        }
                    }
                }
                if (FD_ISSET(fd, &constant_write_set)) {
                    desc_ready -= 1;
                    ret_val = write_message_to(fd);
                    if (ret_val == status_code::FAIL) {
                        log_error("Error occurred while handling the write message");
                    }
                }
            }
        }
    }

    void http_proxy::shutdown() {
        if (is_running) {
            is_running = false;
            send_terminate(0);
        }
    }

    void http_proxy::free_resources() {
        log("Shutdown...");
        log("Free cache of " + std::to_string(cache_size_bytes()) + " bytes");
        cache->clear();
        for (auto & client : *clients) {
            for (const auto& msg : client.second.message_queue) {
                delete msg.second;
            }
        }
        for (int fd = 3; fd <= selected->get_max_fd(); ++fd) {
            if (FD_ISSET(fd, selected->get_read_set()) ||
                FD_ISSET(fd, selected->get_write_set())) {
                int ret_val = close_connection(fd);
                if (ret_val == status_code::FAIL) {
                    log_error_with_errno("Error in close_connection");
                }
            }
        }
    }

    http_proxy::http_proxy(bool print_allowed) {
        this->print_allowed = print_allowed;
        selected = new io_operations::select_data();
        clients = new std::map<int, client_info>();
        servers = new std::map<int, server_info>();
        cache = new aiwannafly::map_cache<resource_info>();
        DNS_map = new std::map<std::string, struct hostent *>();
    }

    http_proxy::~http_proxy() {
        delete selected;
        delete clients;
        delete servers;
        delete cache;
        delete DNS_map;
    }

    // returns proxy socket fd if success, status_code::FAIL otherwise
    int http_proxy::init_and_bind_proxy_socket(int port) {
        proxy_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (proxy_socket == status_code::FAIL) {
            log_error_with_errno("Error in socket()");
            return status_code::FAIL;
        }
        int return_value = socket_operations::set_reusable(proxy_socket);
        if (return_value == status_code::FAIL) {
            close(proxy_socket);
            log_error("Failed to make socket reusable");
            return status_code::FAIL;
        }
        return_value = socket_operations::set_nonblocking(proxy_socket);
        if (return_value == status_code::FAIL) {
            close(proxy_socket);
            log_error("Failed to make socket reusable");
            return status_code::FAIL;
        }
        struct sockaddr_in proxy_sockaddr{};
        proxy_sockaddr.sin_family = AF_INET;
        proxy_sockaddr.sin_addr.s_addr = INADDR_ANY;
        proxy_sockaddr.sin_port = htons(port);
        return_value = bind(proxy_socket, (struct sockaddr *) &proxy_sockaddr, sizeof(proxy_sockaddr));
        if (return_value < 0) {
            log_error_with_errno("Error in bind");
            return_value = close(proxy_socket);
            if (return_value == status_code::FAIL) {
                log_error_with_errno("Error in close");
            }
            return status_code::FAIL;
        }
        return proxy_socket;
    }

    int http_proxy::close_connection(int fd) {
        assert(fd > 0);
        servers->erase(fd);
        if (servers->contains(fd)) {
            for (const auto& msg : servers->at(fd).message_queue) {
                delete msg;
            }
        }
        clients->erase(fd);
        selected->remove_fd(fd, io_operations::select_data::READ);
        selected->remove_fd(fd, io_operations::select_data::WRITE);
        int ret_val = close(fd);
        return ret_val;
    }

    int http_proxy::read_message_from(int fd) {
        assert(fd > 0);
        io_operations::message *message = io_operations::read_all(fd);
        if (message == nullptr) {
            return status_code::FAIL;
        }
        if (message->len == 0) {
            int ret_val = close_connection(fd);
            if (ret_val == status_code::FAIL) {
                log_error_with_errno("Error in close connection");
                return status_code::FAIL;
            }
            delete message;
            return ret_val;
        }
        if (fd == signal_pipe[io_operations::READ_PIPE_END]) {
            if (std::string(message->data) == TERMINATE_CMD) {
                delete message;
                return status_code::TERMINATE;
            }
        }
        // so, we've got message. what's next?
        // let's check out who sent it
        if (servers->contains(fd)) {
            int ret_val = read_server_response(fd, message);
            if (ret_val == status_code::FAIL) {
                delete message;
            }
            return ret_val;
        } else if (clients->contains(fd)) {
            int ret_val = read_client_request(fd, message);
            if (ret_val == status_code::FAIL) {
                delete message;
            }
            return ret_val;
        }
        delete message;
        return status_code::FAIL;
    }

    int http_proxy::accept_new_client() {
        int new_client_fd = accept(proxy_socket, nullptr, nullptr);
        if (new_client_fd == status_code::FAIL) {
            if (errno != EAGAIN) {
                perror("[PROXY] Error in accept. Shutdown server...");
            }
            return status_code::FAIL;
        }
        int return_value = socket_operations::set_nonblocking(new_client_fd);
        if (return_value == status_code::FAIL) {
            close(new_client_fd);
            return status_code::FAIL;
        }
        selected->add_fd(new_client_fd, io_operations::select_data::READ);
        (*clients)[new_client_fd] = client_info();
        return status_code::SUCCESS;
    }

    int http_proxy::write_message_to(int fd) {
        selected->remove_fd(fd, io_operations::select_data::WRITE);
        if (servers->contains(fd)) {
            assert(!clients->contains(fd));
            if (servers->at(fd).status == NOT_CONNECTED) {
                int ret_val = finish_connect_to_remote(fd);
                if (ret_val == status_code::FAIL) {
                    log_error_with_errno("Failed to make connection");
                } else {
                    if (!servers->at(fd).message_queue.empty()) {
                        selected->add_fd(fd, io_operations::select_data::WRITE);
                    }
                }
            } else {
                size_t msg_count = servers->at(fd).message_queue.size();
                if (msg_count >= 1) {
                    io_operations::message *message = servers->at(fd).message_queue.at(msg_count - 1);
                    servers->at(fd).message_queue.pop_back();
                    bool written = io_operations::write_all(fd, message);
                    delete message;
                    if (msg_count - 1 > 0) {
                        selected->add_fd(fd, io_operations::select_data::WRITE);
                    }
                    if (!written) {
                        log_error_with_errno("Error in write all");
                        return status_code::FAIL;
                    } else {
                        log("Sent request");
                    }
                }
            }
        }
        if (clients->contains(fd)) {
            assert(!servers->contains(fd));
            size_t msg_count = clients->at(fd).message_queue.size();
            if (msg_count >= 1) {
                std::pair<std::string, io_operations::message *> pair;
                pair = clients->at(fd).message_queue.at(msg_count - 1);
                clients->at(fd).message_queue.pop_back();
                bool written = io_operations::write_all(fd, pair.second);
                if (!cache->contains(pair.first)) {
                    delete pair.second;
                }
                if (msg_count - 1 > 0) {
                    selected->add_fd(fd, io_operations::select_data::WRITE);
                }
                if (!written) {
                    log_error_with_errno("Error in write all");
                    return status_code::FAIL;
                } else {
                    log("Sent response to client");
                }
            }
        }
        return status_code::SUCCESS;
    }

    int http_proxy::begin_connect_to_remote(const std::string &hostname, int port) {
        if (port < 0 || port >= 65536) {
            return status_code::FAIL;
        }
        int sd = socket(AF_INET, SOCK_STREAM, 0);
        if (sd == status_code::FAIL) {
            return status_code::FAIL;
        }
        struct hostent *hostnm;
        if (DNS_map->contains(hostname)) {
            hostnm = DNS_map->at(hostname);
        } else {
            hostnm = gethostbyname(hostname.data());
            if (hostnm == nullptr) {
                log_error_with_errno("Failed to get by hostname");
                return status_code::FAIL;
            }
        }
        (*DNS_map)[hostname] = hostnm;
        struct sockaddr_in serv_sockaddr{};
        serv_sockaddr.sin_family = AF_INET;
        serv_sockaddr.sin_port = htons(port);
        serv_sockaddr.sin_addr.s_addr = *((unsigned long *) hostnm->h_addr);
        int opt = fcntl(sd, F_GETFL, NULL);
        if (opt < 0) {
            close(sd);
            return status_code::FAIL;
        }
        int return_code = fcntl(sd, F_SETFL, opt | O_NONBLOCK);
        if (return_code < 0) {
            close(sd);
            return status_code::FAIL;
        }
        return_code = connect(sd, (const struct sockaddr *) &serv_sockaddr, sizeof(serv_sockaddr));
        if (return_code < 0) {
            if (errno == EINPROGRESS) {
                selected->add_fd(sd, io_operations::select_data::WRITE);
                (*servers)[sd] = server_info();
                (*servers)[sd].status = NOT_CONNECTED;
                return sd;
            }
            return status_code::FAIL;
        }
        selected->add_fd(sd, io_operations::select_data::READ);
        (*servers)[sd] = server_info();
        (*servers)[sd].status = CONNECTED;
        return sd;
    }

    int http_proxy::finish_connect_to_remote(int fd) {
        assert((*servers).contains(fd));
        int opt = fcntl(fd, F_GETFL, NULL);
        if (opt < 0) {
            return status_code::FAIL;
        }
        socklen_t len = sizeof(opt);
        int return_code = getsockopt(fd, SOL_SOCKET, SO_ERROR, &opt, &len);
        if (return_code < 0) {
            return status_code::FAIL;
        }
        if (opt != status_code::SUCCESS) {
            errno = opt;
            return status_code::FAIL;
        }
        selected->add_fd(fd, io_operations::select_data::READ);
        servers->at(fd).status = CONNECTED;
        return status_code::SUCCESS;
    }

    int http_proxy::read_client_request(int client_fd, io_operations::message *request_message) {
        assert(request_message);
        assert(clients->contains(client_fd));
        httpparser::Request request;
        httpparser::HttpRequestParser parser;
        httpparser::HttpRequestParser::ParseResult res = parser.parse(request,
                                                                      request_message->data,
                                                                      request_message->data + request_message->len);
        if (res != httpparser::HttpRequestParser::ParsingCompleted) {
            return status_code::FAIL;
        }
        log(request.method);
        log(request.uri);
        log(std::string("HTTP Version is 1." + std::to_string(request.versionMinor)));
        std::string resource_name = request.uri;
        if (request.method != "GET") {
            return status_code::FAIL;
        }
        if (cache->contains(resource_name)) {
            // we've already tried to get the resource
            log("Found " + resource_name + " in cache");
            resource_info *resource = cache->get(resource_name);
            if (resource->status == httpparser::HttpResponseParser::ParsingCompleted ||
                    resource->status == httpparser::HttpResponseParser::ParsingIncompleted) {
                log("It is completed");
                assert(!resource->parts.empty());
                for (auto msg : resource->parts) {
                    clients->at(client_fd).message_queue.emplace_back(resource_name, msg);
                }
                selected->add_fd(client_fd, io_operations::select_data::WRITE);
                resource->subscribers.insert(client_fd);
                return status_code::SUCCESS;
            }
            return status_code::FAIL;
        }
        for (const httpparser::Request::HeaderItem &header: request.headers) {
            log(header.name + std::string(" : ") + header.value);
            if (header.name == "Host") {
                int ret_val = begin_connect_to_remote(header.value, HTTP_PORT);
                if (ret_val == status_code::FAIL) {
                    log_error_with_errno("Failed to connect to remote");
                    return status_code::FAIL;
                }
                int sd = ret_val;
                assert(servers->contains(sd));
                servers->at(sd).message_queue.push_back(request_message);
                if (!cache->contains(resource_name)) {
                    cache->put(resource_name, new resource_info());
                }
                cache->get(resource_name)->subscribers.insert(client_fd);
                servers->at(sd).resource_name = resource_name;
                log(std::string("Started connecting to " + header.value));
                return ret_val;
            }
        }
        log_error("Not found host header");
        return status_code::FAIL;
    }

    size_t http_proxy::cache_size_bytes() {
        return cache->size_bytes([](const resource_info *r) -> size_t {
            size_t total_size = sizeof(r->subscribers) + sizeof(r->status);
            for (auto msg : r->parts) {
                total_size += io_operations::message_size(msg);
            }
            return total_size;
        });
    }

    void http_proxy::send_resource_part(const std::string &resource_name, resource_info *resource) {
        if (resource->parts.empty()) return;
        io_operations::message *full_message = resource->parts.back();
        for (int client_fd: resource->subscribers) {
            if (!clients->contains(client_fd)) {
                continue;
            }
            selected->add_fd(client_fd, io_operations::select_data::WRITE);
            clients->at(client_fd).message_queue.emplace_back(resource_name, full_message);
        }
    }

    int http_proxy::read_server_response(int server_fd, io_operations::message *new_part) {
        auto resource_name = servers->at(server_fd).resource_name;
        auto resource = cache->get(resource_name);
        if (resource->status == httpparser::HttpResponseParser::ParsingCompleted) {
            return status_code::SUCCESS;
        }
        io_operations::message *full_message = new_part;
        if (resource->full_data != nullptr) {
            full_message = resource->full_data;
            bool added = io_operations::append_message(resource->full_data, new_part);
            if (!added) {
                return status_code::FAIL;
            }
        } else {
            resource->full_data = io_operations::copy(new_part);
        }
        resource->parts.push_back(new_part);
        resource->current_length += new_part->len;
        if (resource->content_length >= resource->current_length) {
            // send the current part to clients
            send_resource_part(resource_name, resource);
            if (resource->content_length == resource->current_length) {
                resource->status = httpparser::HttpResponseParser::ParsingCompleted;
                log("Cache size: " + std::to_string(cache_size_bytes()));
            }
            return status_code::SUCCESS;
        }
        httpparser::Response response;
        httpparser::HttpResponseParser parser;
        httpparser::HttpResponseParser::ParseResult res = parser.parse(response, full_message->data,
                                                                       full_message->data + full_message->len);
        if (res == httpparser::HttpResponseParser::ParsingError) {
            log_error("Failed to parse response of " + resource_name);
            return status_code::FAIL;
        }
        if (res == httpparser::HttpResponseParser::ParsingIncompleted) {
            auto content_length_header = std::find_if(response.headers.begin(), response.headers.end(),
                                                      [&](const httpparser::Response::HeaderItem& item){
                                                          return item.name == "Content-Length";
            });
            if (content_length_header != response.headers.end()) {
                size_t content_length = std::stoul(content_length_header->value);
                resource->content_length = content_length;
                assert(response.content.size() < full_message->len);
                resource->content_length += (full_message->len - response.content.size());
                log("Content-Length : " + content_length_header->value);
            }
            log("Response of " + resource_name + " is not complete, it's current length: " +
            std::to_string(full_message->len));
            assert(resource->status == httpparser::HttpResponseParser::ParsingIncompleted);
            send_resource_part(resource_name, resource);
            return status_code::SUCCESS;
        }
        log("Parsed response of " + resource_name + std::string(" code: ") + std::to_string(response.statusCode));
        resource->status = httpparser::HttpResponseParser::ParsingCompleted;
        send_resource_part(resource_name, resource);
        if (response.statusCode != 200) {
            log("Bad status code, not store " + resource_name + " in cache");
            cache->get(resource_name)->free_message = false;
            delete cache->get(resource_name);
            cache->erase(resource_name);
        }
        log("Cache size: " + std::to_string(cache_size_bytes()));
        return status_code::SUCCESS;
    }
}
