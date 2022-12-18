#include <unistd.h>
#include <csignal>
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <cstring>
#include <netdb.h>
#include <fcntl.h>
#include <cassert>

#include "../httpparser/src/httpparser/request.h"
#include "../httpparser/src/httpparser/httprequestparser.h"

#include "single_thread_proxy.h"
#include "status_code.h"
#include "socket_operations.h"

#define TERMINATE_CMD "stop"

namespace single_thread_proxy {
    static const int MAX_LISTEN_QUEUE_SIZE = 500;
    static const int HTTP_PORT = 80;
    int signal_pipe[2];

    void HttpProxy::log(const std::string &msg) const {
        if (!print_allowed) return;
        std::cout << "[PROXY] " << msg << std::endl;
    }

    void HttpProxy::logError(const std::string &msg) const {
        if (!print_allowed) return;
        std::cerr << "[PROXY] " << msg << std::endl;
    }

    void HttpProxy::logErrorWithErrno(const std::string &msg) const {
        if (!print_allowed) return;
        perror(("[PROXY] " + msg).data());
    }

    void sendTerminate(__attribute__((unused)) int sig) {
        auto *terminate = new io::Message();
        char *cmd = (char *) malloc(strlen(TERMINATE_CMD) + 1);
        if (cmd == nullptr) {
            return;
        }
        strcpy(cmd, TERMINATE_CMD);
        terminate->data = cmd;
        terminate->len = strlen(TERMINATE_CMD);
        io::WriteAll(signal_pipe[io::WRITE_PIPE_END], terminate);
        delete terminate;
    }

    int initSignalHandlers() {
        int return_value = pipe(signal_pipe);
        if (return_value == status_code::FAIL) {
            return status_code::FAIL;
        }
        signal(SIGINT, sendTerminate);
        signal(SIGTERM, sendTerminate);
        return status_code::SUCCESS;
    }

    void HttpProxy::run(int port) {
        is_running = true;
        int ret_val = initAndBindProxySocket(port);
        if (ret_val == status_code::FAIL) {
            logError("Could not init and bind proxy socket");
            return;
        }
        ret_val = initSignalHandlers();
        if (ret_val == status_code::FAIL) {
            logErrorWithErrno("Could not init signal handlers");
            return;
        }
        ret_val = listen(proxy_socket, MAX_LISTEN_QUEUE_SIZE);
        if (ret_val == status_code::FAIL) {
            logErrorWithErrno("Error in listen");
            ret_val = close(proxy_socket);
            if (ret_val == status_code::FAIL) {
                logErrorWithErrno("Error in close");
            }
        }
        selected->addFd(proxy_socket, io::SelectData::READ);
        selected->addFd(signal_pipe[io::READ_PIPE_END], io::SelectData::READ);
        log("Running on " + std::to_string(port));
        fd_set constant_read_set;
        fd_set constant_write_set;
        while (true) {
//            log("Waiting on select");
            memcpy(&constant_read_set, selected->getReadSet(), sizeof(*selected->getReadSet()));
            memcpy(&constant_write_set, selected->getWriteSet(), sizeof(*selected->getWriteSet()));
            ret_val = select(selected->getMaxFd() + 1, &constant_read_set, &constant_write_set,
                             nullptr, nullptr);
            if (ret_val == status_code::FAIL || ret_val == status_code::TIMEOUT) {
                if (errno != EINTR && ret_val == status_code::FAIL) {
                    logErrorWithErrno("Error in select");
                    freeResources();
                    return;
                }
                if (ret_val == status_code::TIMEOUT) {
                    log("Select timed out. End program.");
                    freeResources();
                    return;
                }
            }
            int desc_ready = ret_val;
            for (int fd = 0; fd <= selected->getMaxFd() && desc_ready > 0; ++fd) {
                if (FD_ISSET(fd, &constant_read_set)) {
                    desc_ready -= 1;
                    if (fd == proxy_socket) {
                        log("Handle new connection");
                        ret_val = acceptNewClient();
                        if (ret_val == status_code::FAIL) {
                            logErrorWithErrno("Failed to accept new connection");
                            break;
                        }
                    } else {
//                        log("Handle new message");
                        ret_val = readMessageFrom(fd);
                        if (ret_val == status_code::TERMINATE) {
                            freeResources();
                            return;
                        }
                    }
                }
                if (FD_ISSET(fd, &constant_write_set)) {
                    desc_ready -= 1;
                    ret_val = writeMessageTo(fd);
                    if (ret_val == status_code::FAIL) {
                        logError("Error occurred while handling the write message");
                    }
                }
            }
        }
    }

    void HttpProxy::shutdown() {
        if (is_running) {
            is_running = false;
            sendTerminate(0);
        }
    }

    void HttpProxy::freeResources() {
        log("Shutdown...");
        log("Free cache of " + std::to_string(cacheSizeBytes()) + " bytes");
        cache->clear();
        int ret_val = close(proxy_socket);
        if (ret_val == status_code::FAIL) {
            logErrorWithErrno("Error in close(proxy_socket)");
        }
        for (int fd = 3; fd <= selected->getMaxFd(); fd++) {
            if (fd == proxy_socket) continue;
            if (FD_ISSET(fd, selected->getReadSet()) ||
                FD_ISSET(fd, selected->getWriteSet())) {
                ret_val = closeConnection(fd);
                if (ret_val == status_code::FAIL) {
                    logErrorWithErrno("Error in close_connection");
                }
            }
        }
    }

    HttpProxy::HttpProxy(bool print_allowed) {
        this->print_allowed = print_allowed;
        selected = new io::SelectData();
        clients = new std::map<int, ClientInfo>();
        servers = new std::map<int, ServerInfo>();
        cache = new aiwannafly::MapCache<ResourceInfo>();
        DNS_map = new std::map<std::string, struct hostent *>();
    }

    HttpProxy::~HttpProxy() {
        delete selected;
        delete clients;
        delete servers;
        delete cache;
        delete DNS_map;
    }

    // returns proxy socket fd if success, status_code::FAIL otherwise
    int HttpProxy::initAndBindProxySocket(int port) {
        proxy_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (proxy_socket == status_code::FAIL) {
            logErrorWithErrno("Error in socket()");
            return status_code::FAIL;
        }
        int return_value = socket_operations::SetReusable(proxy_socket);
        if (return_value == status_code::FAIL) {
            close(proxy_socket);
            logError("Failed to make socket reusable");
            return status_code::FAIL;
        }
        return_value = socket_operations::SetNonblocking(proxy_socket);
        if (return_value == status_code::FAIL) {
            close(proxy_socket);
            logError("Failed to make socket reusable");
            return status_code::FAIL;
        }
        struct sockaddr_in proxy_sockaddr{};
        proxy_sockaddr.sin_family = AF_INET;
        proxy_sockaddr.sin_addr.s_addr = INADDR_ANY;
        proxy_sockaddr.sin_port = htons(port);
        return_value = bind(proxy_socket, (struct sockaddr *) &proxy_sockaddr, sizeof(proxy_sockaddr));
        if (return_value < 0) {
            logErrorWithErrno("Error in bind");
            return_value = close(proxy_socket);
            if (return_value == status_code::FAIL) {
                logErrorWithErrno("Error in close");
            }
            return status_code::FAIL;
        }
        return proxy_socket;
    }

    int HttpProxy::closeConnection(int fd) {
        assert(fd > 0);
        servers->erase(fd);
        if (servers->contains(fd)) {
            for (const auto &msg: servers->at(fd).message_queue) {
                delete msg;
            }
        }
        clients->erase(fd);
        selected->remove_fd(fd, io::SelectData::READ);
        selected->remove_fd(fd, io::SelectData::WRITE);
        int ret_val = close(fd);
        return ret_val;
    }

    int HttpProxy::readMessageFrom(int fd) {
        assert(fd > 0);
        io::Message *message = io::read_all(fd);
        if (message == nullptr) {
            return status_code::FAIL;
        }
        if (message->len == 0) {
            int ret_val = closeConnection(fd);
            if (ret_val == status_code::FAIL) {
                logErrorWithErrno("Error in close connection");
                return status_code::FAIL;
            }
            delete message;
            return ret_val;
        }
        if (fd == signal_pipe[io::READ_PIPE_END]) {
            if (std::string(message->data) == TERMINATE_CMD) {
                delete message;
                return status_code::TERMINATE;
            }
        }
        // so, we've got message. what's next?
        // let's check out who sent it
        if (servers->contains(fd)) {
            int ret_val = readServerResponse(fd, message);
            if (ret_val == status_code::FAIL) {
                delete message;
            }
            return ret_val;
        } else if (clients->contains(fd)) {
            int ret_val = readClientRequest(fd, message);
            if (ret_val == status_code::FAIL) {
                delete message;
            }
            return ret_val;
        }
        delete message;
        return status_code::FAIL;
    }

    int HttpProxy::acceptNewClient() {
        int new_client_fd = accept(proxy_socket, nullptr, nullptr);
        if (new_client_fd == status_code::FAIL) {
            if (errno != EAGAIN) {
                perror("[PROXY] Error in accept. Shutdown server...");
            }
            return status_code::FAIL;
        }
        int return_value = socket_operations::SetNonblocking(new_client_fd);
        if (return_value == status_code::FAIL) {
            close(new_client_fd);
            return status_code::FAIL;
        }
        selected->addFd(new_client_fd, io::SelectData::READ);
        (*clients)[new_client_fd] = ClientInfo();
        log("Accepted new client");
        return status_code::SUCCESS;
    }

    int HttpProxy::writeMessageTo(int fd) {
        selected->remove_fd(fd, io::SelectData::WRITE);
        if (servers->contains(fd)) {
            assert(!clients->contains(fd));
            if (!servers->at(fd).connected) {
                int ret_val = finishConnectToServer(fd);
                if (ret_val == status_code::FAIL) {
                    logErrorWithErrno("Failed to make connection");
                } else {
                    if (!servers->at(fd).message_queue.empty()) {
                        selected->addFd(fd, io::SelectData::WRITE);
                    }
                }
            } else {
                size_t msg_count = servers->at(fd).message_queue.size();
                if (msg_count >= 1) {
                    io::Message *message = servers->at(fd).message_queue.back();
                    servers->at(fd).message_queue.pop_back();
                    bool written = io::WriteAll(fd, message);
                    delete message;
                    if (msg_count - 1 > 0) {
                        selected->addFd(fd, io::SelectData::WRITE);
                    }
                    if (!written) {
                        logErrorWithErrno("Error in write all");
                        return status_code::FAIL;
                    }
                }
            }
        }
        if (clients->contains(fd)) {
            assert(!servers->contains(fd));
            size_t msg_count = clients->at(fd).message_queue.size();
            if (msg_count >= 1) {
                auto msg = clients->at(fd).message_queue.front();
                clients->at(fd).message_queue.erase(clients->at(fd).message_queue.begin());
                bool written = io::WriteAll(fd, msg);
                if (!cache->contains(clients->at(fd).res_name)) {
                    delete msg;
                }
                if (msg_count - 1 > 0) {
                    selected->addFd(fd, io::SelectData::WRITE);
                }
                if (!written) {
                    logErrorWithErrno("Error in write all");
                    return status_code::FAIL;
                } else {
//                    log("Sent to client " + std::to_string(len) + " bytes");
                }
            }
        }
        return status_code::SUCCESS;
    }

    int HttpProxy::beginConnectToServer(const std::string &hostname, int port) {
        if (port < 0 || port >= 65536) {
            return status_code::FAIL;
        }
        int sd = socket(AF_INET, SOCK_STREAM, 0);
        if (sd == status_code::FAIL) {
            return status_code::FAIL;
        }
        int return_code = socket_operations::SetNonblocking(sd);
        if (return_code == status_code::FAIL) {
            return status_code::FAIL;
        }
        struct hostent *hostnm;
        if (DNS_map->contains(hostname)) {
            hostnm = DNS_map->at(hostname);
        } else {
            hostnm = gethostbyname(hostname.data());
            if (hostnm == nullptr) {
                logErrorWithErrno("Failed to get by hostname");
                return status_code::FAIL;
            }
        }
        (*DNS_map)[hostname] = hostnm;
        struct sockaddr_in serv_sockaddr{};
        serv_sockaddr.sin_family = AF_INET;
        serv_sockaddr.sin_port = htons(port);
        serv_sockaddr.sin_addr.s_addr = *((unsigned long *) hostnm->h_addr);
        return_code = connect(sd, (const struct sockaddr *) &serv_sockaddr, sizeof(serv_sockaddr));
        if (return_code < 0) {
            if (errno == EINPROGRESS) {
                selected->addFd(sd, io::SelectData::WRITE);
                (*servers)[sd] = ServerInfo();
                (*servers)[sd].connected = false;
                return sd;
            }
            return status_code::FAIL;
        }
        selected->addFd(sd, io::SelectData::READ);
        (*servers)[sd] = ServerInfo();
        (*servers)[sd].connected = true;
        return sd;
    }

    int HttpProxy::finishConnectToServer(int fd) {
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
        selected->addFd(fd, io::SelectData::READ);
        servers->at(fd).connected = true;
        return status_code::SUCCESS;
    }

    int HttpProxy::readClientRequest(int client_fd, io::Message *request_message) {
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
        std::string res_name = request.uri;
        if (request.method != "GET") {
            return status_code::FAIL;
        }
        if (cache->contains(res_name)) {
            // we've already tried to get the resource
            log("Found " + res_name + " in cache");
            ResourceInfo *resource = cache->get(res_name);
            resource->subscribers.insert(client_fd);
            log("It's size : " + std::to_string(resource->current_length));
            log("It's count of parts : " + std::to_string(resource->parts.size()));
            clients->at(client_fd).res_name = res_name;
            for (auto msg: resource->parts) {
                clients->at(client_fd).message_queue.push_back(msg);
            }
            if (!clients->at(client_fd).message_queue.empty()) {
                selected->addFd(client_fd, io::SelectData::WRITE);
            }
            return status_code::FAIL;
        }
        for (const httpparser::Request::HeaderItem &header: request.headers) {
            log(header.name + std::string(" : ") + header.value);
            if (header.name == "Host") {
                int ret_val = beginConnectToServer(header.value, HTTP_PORT);
                if (ret_val == status_code::FAIL) {
                    logErrorWithErrno("Failed to connect to remote");
                    return status_code::FAIL;
                }
                int sd = ret_val;
                assert(servers->contains(sd));
                servers->at(sd).message_queue.push_back(request_message);
                if (!cache->contains(res_name)) {
                    cache->put(res_name, new ResourceInfo());
                }
                cache->get(res_name)->subscribers.insert(client_fd);
                if (servers->contains(sd)) {
                    log("Server res name : " + servers->at(sd).res_name);
                }
                servers->at(sd).res_name = res_name;
                log(std::string("Started connecting to " + header.value));
                return ret_val;
            }
        }
        logError("Not found host header");
        return status_code::FAIL;
    }

    size_t HttpProxy::cacheSizeBytes() {
        return cache->sizeBytes([](const ResourceInfo *r) -> size_t {
            size_t total_size = 0;
            for (auto msg: r->parts) {
                total_size += msg->len;
            }
            return total_size;
        });
    }

    void HttpProxy::sendLastResourcePart(const std::string &resource_name, ResourceInfo *resource) {
        if (resource->parts.empty()) return;
        io::Message *full_message = resource->parts.back();
        if (resource->parts.size() == 1) {
            log("Count of subscribers : " + std::to_string(resource->subscribers.size()));
        }
        for (int client_fd: resource->subscribers) {
            if (!clients->contains(client_fd)) {
                if (resource->parts.size() == 1) log("Don't work with the client");
                continue;
            }
            selected->addFd(client_fd, io::SelectData::WRITE);
            clients->at(client_fd).res_name = resource_name;
            clients->at(client_fd).message_queue.push_back(full_message);
        }
    }

    int HttpProxy::readServerResponse(int server_fd, io::Message *new_part) {
        auto res_name = servers->at(server_fd).res_name;
        if (!cache->contains(res_name)) {
            cache->put(res_name, new ResourceInfo);
        }
        auto resource = cache->get(res_name);
        resource->parts.push_back(new_part);
        io::Message *full_msg = new_part;
        if (resource->content_length == 0) {
            if (resource->full_data != nullptr) {
                full_msg = resource->full_data;
                bool added = io::AppendMsg(resource->full_data, new_part);
                if (!added) {
                    return status_code::FAIL;
                }
            } else {
                resource->full_data = io::copy(new_part);
            }
        }
        resource->current_length += new_part->len;
        if (resource->content_length > resource->current_length) {
            sendLastResourcePart(res_name, resource);
            return status_code::SUCCESS;
        } else if (resource->content_length == resource->current_length && resource->content_length > 0) {
            sendLastResourcePart(res_name, resource);
            resource->status = httpparser::HttpResponseParser::ParsingCompleted;
            delete resource->full_data;
            resource->full_data = nullptr;
            log("Cache size: " + std::to_string(cacheSizeBytes()));
            return status_code::SUCCESS;
        }
        httpparser::Response response;
        httpparser::HttpResponseParser parser;
        httpparser::HttpResponseParser::ParseResult res = parser.parse(response, full_msg->data,
                                                                       full_msg->data + full_msg->len);
        if (res == httpparser::HttpResponseParser::ParsingError) {
            logError("Failed to parse response of " + res_name);
            return status_code::FAIL;
        }
        sendLastResourcePart(res_name, resource);
        if (res == httpparser::HttpResponseParser::ParsingIncompleted) {
            auto content_length_header = std::find_if(response.headers.begin(), response.headers.end(),
                                                      [&](const httpparser::Response::HeaderItem &item) {
                                                          return item.name == "Content-Length";
                                                      });
            if (content_length_header != response.headers.end()) {
                size_t content_length = std::stoul(content_length_header->value);
                resource->content_length = content_length;
                assert(full_msg->len > response.content.size());
                size_t sub = full_msg->len - response.content.size();
                if (sub > 0) {
                    resource->content_length += sub;
                }
                log("Content-Length : " + std::to_string(resource->content_length));
            }
            log("Response of " + res_name + " is not complete, it's current length: " +
                std::to_string(full_msg->len));
            assert(resource->status == httpparser::HttpResponseParser::ParsingIncompleted);
            return status_code::SUCCESS;
        }
        log("Parsed response of " + res_name + std::string(" code: ") + std::to_string(response.statusCode));
        resource->status = httpparser::HttpResponseParser::ParsingCompleted;
        log("Full bytes length : " + std::to_string(resource->full_data->len));
        log("Full parts count : " + std::to_string(resource->parts.size()));
        delete resource->full_data;
        resource->full_data = nullptr;
        if (response.statusCode != 200) {
            log("Status code is not 200, not store " + res_name + " in cache");
            cache->get(res_name)->free_messages = false;
            delete cache->get(res_name);
            cache->erase(res_name);
        }
        log("Cache size: " + std::to_string(cacheSizeBytes()));
        return status_code::SUCCESS;
    }
}
