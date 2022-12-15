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
#include "io_operations.h"
#include "socket_operations.h"

namespace single_thread_proxy {
    static const int HTTP_PORT = 80;
    static const std::string terminate_cmd = "stop";
    int signal_pipe[2];

    void send_terminate(__attribute__((unused)) int sig) {
        auto *terminate = new io_operations::message(terminate_cmd);
        io_operations::write_all(
                signal_pipe[io_operations::WRITE_PIPE_END], terminate);
        delete terminate;
    }

    int init_signal_handlers() {
        int return_value = pipe(signal_pipe);
        if (return_value == status_code::FAIL) {
            perror("[PROXY] Error in pipe()");
            return status_code::FAIL;
        }
        signal(SIGINT, send_terminate);
        signal(SIGTERM, send_terminate);
        return status_code::SUCCESS;
    }

    void http_proxy::log(const std::string &msg) {
        std::cout << "[PROXY] " << msg << std::endl;
    }

    void http_proxy::log_error(const std::string &msg) {
        std::cerr << "[PROXY] " << msg << std::endl;
    }

    void http_proxy::log_error_with_errno(const std::string &msg) {
        perror(msg.data());
    }

    void http_proxy::run(int port) {
        is_running = true;
        int ret_val = init_and_bind_proxy_socket(port);
        if (ret_val == status_code::FAIL) {
            log_error("Could not init and bind proxy socket");
            return;
        }
        ret_val = listen(proxy_socket, MAX_CLIENTS_COUNT);
        if (ret_val == status_code::FAIL) {
            log_error_with_errno("Error in listen");
            ret_val = close(proxy_socket);
            if (ret_val == status_code::FAIL) {
                log_error_with_errno("Error in close");
            }
        }
        selected->add_fd(proxy_socket, io_operations::select_data::READ);
        selected->add_fd(signal_pipe[io_operations::READ_PIPE_END], io_operations::
        select_data::READ);
        struct timeval timeout{
                .tv_sec = MAX_WAIT_TIME_SECS,
                .tv_usec = 0
        };
        log("Running...");
        fd_set constant_read_set;
        fd_set constant_write_set;
        while (is_running) {
            log("Waiting on select");
            memcpy(&constant_read_set, selected->get_read_set(), sizeof(*selected->get_read_set()));
            memcpy(&constant_write_set, selected->get_write_set(), sizeof(*selected->get_write_set()));
            ret_val = select(selected->get_max_fd() + 1, &constant_read_set, &constant_write_set,
                             nullptr, &timeout);
            if (ret_val == status_code::FAIL || ret_val == status_code::TIMEOUT) {
                if (errno != EINTR) log_error_with_errno("Error in select");
                if (ret_val == status_code::TIMEOUT) {
                    log("Select timed out. End program.");
                    close_all_connections();
                    return;
                }
                break;
            }
            int desc_ready = ret_val;
            for (int fd = 0; fd <= selected->get_max_fd() && desc_ready > 0; ++fd) {
                if (FD_ISSET(fd, &constant_write_set)) {
                    desc_ready -= 1;
                    ret_val = handle_write_message(fd);
                    if (ret_val == status_code::FAIL) {
                        log_error("Error occurred while handling the write message");
                    }
                }
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
                        ret_val = handle_read_message(fd);
                        if (ret_val == status_code::TERMINATE) {
                            close_all_connections();
                            return;
                        }
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

    void http_proxy::close_all_connections() {
        log("Shutdown...");
        for (int fd = 0; fd <= selected->get_max_fd(); ++fd) {
            if (FD_ISSET(fd, selected->get_read_set()) ||
                FD_ISSET(fd, selected->get_write_set())) {
                int ret_val = close(fd);
                if (ret_val == status_code::FAIL) {
                    log_error_with_errno("Error in close");
                }
            }
        }
    }

    http_proxy::http_proxy() {
        selected = new io_operations::select_data();
        clients = new std::map<int, client_info>();
        servers = new std::map<int, server_info>();
        resources = new std::map<std::string, int>();
    }

    http_proxy::~http_proxy() {
        delete selected;
        delete clients;
        delete servers;
        delete resources;
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
        clients->erase(fd);
        selected->remove_fd(fd, io_operations::select_data::READ);
        selected->remove_fd(fd, io_operations::select_data::WRITE);
        int ret_val = close(fd);
        return ret_val;
    }

    int http_proxy::handle_read_message(int fd) {
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
        // so, we've got message. what's next?
        // let's check out who sent it
        if (servers->contains(fd)) {
            int ret_val = handle_server_response(fd, message);
            if (ret_val == status_code::FAIL) {
                delete message;
            }
            return ret_val;
        } else if (clients->contains(fd)) {
            int ret_val = handle_client_request(fd, message);
            if (ret_val == status_code::FAIL) {
                delete message;
            }
            return ret_val;
        }
        delete message;
        return status_code::SUCCESS;
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

    int http_proxy::handle_write_message(int fd) {
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
                    if (!written) {
                        log_error_with_errno("Error in write all");
                    } else {
                        log("Sent request");
                    }
                    if (msg_count - 1 > 0) {
                        selected->add_fd(fd, io_operations::select_data::WRITE);
                    }
                }
            }
        }
        if (clients->contains(fd)) {
            assert(!servers->contains(fd));
            size_t msg_count = clients->at(fd).message_queue.size();
            if (msg_count >= 1) {
                io_operations::message *message = clients->at(fd).message_queue.at(msg_count - 1);
                clients->at(fd).message_queue.pop_back();
                bool written = io_operations::write_all(fd, message);
                delete message;
                if (!written) {
                    log_error_with_errno("Error in write all");
                } else {
                    log("Sent response to client");
                }
                if (msg_count - 1 > 0) {
                    selected->add_fd(fd, io_operations::select_data::WRITE);
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
        hostnm = gethostbyname(hostname.data());
        if (hostnm == (struct hostent *) 0) {
            log_error_with_errno("Failed to get by hostname");
            return status_code::FAIL;
        }
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

    int http_proxy::handle_client_request(int client_fd, io_operations::message *request_message) {
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
        if (resources->contains(request.uri)) {
            // we've already tried to get the resource
            resource_info resource = servers->at(resources->at(request.uri)).resource;
            if (resource.status == httpparser::HttpResponseParser::ParsingCompleted) {
                assert(resource.data);
                clients->at(client_fd).message_queue.push_back(resource.data);
                selected->add_fd(client_fd, io_operations::select_data::WRITE);
                return status_code::SUCCESS;
            } else if (resource.status == httpparser::HttpResponseParser::ParsingIncompleted) {
                servers->at(resources->at(request.uri)).resource.subscribers.insert(client_fd);
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
                (*resources)[request.uri] = sd;
                servers->at(sd).message_queue.push_back(request_message);
                servers->at(sd).resource.subscribers.insert(client_fd);
                log(std::string("Started connecting to " + header.value));
                return ret_val;
            }
        }
        log_error("Not found host header");
        return status_code::FAIL;
    }

    int http_proxy::handle_server_response(int server_fd, io_operations::message *response_message) {
        assert(response_message);
        assert(servers->contains(server_fd));
        auto resource = servers->at(server_fd).resource;
        if (resource.status == httpparser::HttpResponseParser::ParsingCompleted) {
            return status_code::SUCCESS;
        }
        io_operations::message *full_message = response_message;
        if (resource.data != nullptr) {
            full_message = io_operations::concat_messages(resource.data, response_message);
        }
        servers->at(server_fd).resource.data = full_message;
        httpparser::Response response;
        httpparser::HttpResponseParser parser;
        httpparser::HttpResponseParser::ParseResult res = parser.parse(response, full_message->data,
                                                                       full_message->data + full_message->len);
        if (res == httpparser::HttpResponseParser::ParsingError) {
            log_error("Failed to parse response");
            return status_code::FAIL;
        }
        if (res == httpparser::HttpResponseParser::ParsingIncompleted) {
            assert(resource.status == httpparser::HttpResponseParser::ParsingIncompleted);
            return status_code::SUCCESS;
        }
        for (int client_fd : resource.subscribers) {
            selected->add_fd(client_fd, io_operations::select_data::WRITE);
            clients->at(client_fd).message_queue.push_back(full_message);
        }
        resource.subscribers.clear();
        return status_code::SUCCESS;
    }
}
