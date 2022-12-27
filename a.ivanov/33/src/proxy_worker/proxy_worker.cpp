#include "proxy_worker.h"

#include <unistd.h>
#include <csignal>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <netdb.h>
#include <fcntl.h>
#include <cassert>
#include <sys/eventfd.h>

#include "../../httpparser/src/httpparser/request.h"
#include "../../httpparser/src/httpparser/httprequestparser.h"

#include "../status_code.h"
#include "../utils/socket_operations.h"
#include "../utils/log.h"

#define TERMINATE_CMD "stop"

namespace worker_thread_proxy {
    static const int HTTP_PORT = 80;

    ProxyWorker::ProxyWorker(int notice_fd, int signal_fd, aiwannafly::Cache<ResourceInfo> *cache) {
        assert(cache);
        selected = new io::SelectData();
        clients = new std::map<int, ClientInfo>();
        servers = new std::map<int, ServerInfo>();
        this->notice_fd = notice_fd;
        this->signal_fd = signal_fd;
        this->cache = cache;
        DNS_map = new std::map<std::string, struct hostent *>();
    }

    ProxyWorker::~ProxyWorker() {
        delete selected;
        delete clients;
        delete servers;
        delete cache;
        delete DNS_map;
    }

    int ProxyWorker::run() {
        selected->addFd(notice_fd, io::SelectData::READ);
        selected->addFd(signal_fd, io::SelectData::READ);
        fd_set constant_read_set;
        fd_set constant_write_set;
        while (true) {
            struct timeval timeout = {
                    .tv_sec = 0,
                    .tv_usec = 200 * 1000
            };
            memcpy(&constant_read_set, selected->getReadSet(), sizeof(*selected->getReadSet()));
            memcpy(&constant_write_set, selected->getWriteSet(), sizeof(*selected->getWriteSet()));
            int ret_val = select(selected->getMaxFd() + 1, &constant_read_set, &constant_write_set,
                             nullptr, &timeout);
            if (ret_val == status_code::FAIL) {
                if (errno != EINTR) {
                    logErrorWithErrno("Error in select");
                    freeResources();
                    return status_code::FAIL;
                }
            }
            int desc_ready = ret_val;
            for (int fd = 0; fd <= selected->getMaxFd() && desc_ready > 0; ++fd) {
                if (FD_ISSET(fd, &constant_read_set)) {
                    desc_ready -= 1;
                    ret_val = readMessageFrom(fd);
                    if (ret_val == status_code::TERMINATE) {
                        logError("Terminate");
                        freeResources();
                        return status_code::TERMINATE;
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

    void ProxyWorker::freeResources() {
        log("Shutdown...");
        for (int fd = 3; fd <= selected->getMaxFd(); fd++) {
            if (FD_ISSET(fd, selected->getReadSet()) ||
                FD_ISSET(fd, selected->getWriteSet())) {
                int ret_val = closeConnection(fd);
                if (ret_val == status_code::FAIL) {
                    logErrorWithErrno("Error in close_connection");
                }
            }
        }
    }

    int ProxyWorker::closeConnection(int fd) {
        assert(fd > 0);
        servers->erase(fd);
        if (servers->contains(fd)) {
            for (const auto &msg: servers->at(fd).message_queue) {
                delete msg;
            }
        }
        if (clients->contains(fd)) {
            logError("Client " + std::to_string(fd) + " disconnects");
            auto res_name = clients->at(fd).res_name;
            if (cache->contains(res_name)) {
                for (auto sub : cache->get(res_name)->subscribers) {
                    if (sub.fd == fd) {
                        cache->get(res_name)->subscribers.erase(sub);
                        break;
                    }
                }
            }
        }
        clients->erase(fd);
        selected->remove_fd(fd, io::SelectData::READ);
        selected->remove_fd(fd, io::SelectData::WRITE);
        return close(fd);
    }

    int ProxyWorker::readMessageFrom(int fd) {
        assert(fd > 0);
        if (fd == notice_fd) {
            eventfd_t new_client_fd;
            int code = eventfd_read(fd, &new_client_fd);
            if (code < 0) {
                return status_code::FAIL;
            } else {
                (*clients)[(int) new_client_fd] = ClientInfo();
                selected->addFd((int) new_client_fd, io::SelectData::READ);
                return status_code::SUCCESS;
            }
        }
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
        if (fd == signal_fd) {
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

    int ProxyWorker::writeMessageTo(int fd) {
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
            if (cache->contains(clients->at(fd).res_name) && msg_count == 0) {
                auto resource = cache->get(clients->at(fd).res_name);
                std::copy(resource->parts.begin() + (int) clients->at(fd).recv_msg_count,
                          resource->parts.end(), std::back_inserter(clients->at(fd).message_queue));
            }
            msg_count = clients->at(fd).message_queue.size();
            if (msg_count >= 1) {
                auto msg = clients->at(fd).message_queue.front();
                clients->at(fd).message_queue.erase(clients->at(fd).message_queue.begin());
                bool written = io::WriteAll(fd, msg);
                if (!cache->contains(clients->at(fd).res_name)) {
                    delete msg;
                }
                msg_count--;
                if (msg_count > 0) {
                    selected->addFd(fd, io::SelectData::WRITE);
                }
                clients->at(fd).recv_msg_count++;
                if (!written) {
                    clients->at(fd).recv_msg_count--;
                    logErrorWithErrno("Error in write all");
                    return status_code::FAIL;
                }
            }
        }
        return status_code::SUCCESS;
    }

    int ProxyWorker::beginConnectToServer(const std::string &hostname, int port) {
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

    int ProxyWorker::finishConnectToServer(int fd) {
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

    int ProxyWorker::readClientRequest(int client_fd, io::Message *request_message) {
        assert(request_message);
        assert(clients->contains(client_fd));
        clients->at(client_fd).recv_msg_count = 0;
        httpparser::Request request;
        httpparser::HttpRequestParser parser;
        httpparser::HttpRequestParser::ParseResult res = parser.parse(request,
                                                                      request_message->data,
                                                                      request_message->data + request_message->len);
        if (res != httpparser::HttpRequestParser::ParsingCompleted) {
            return status_code::FAIL;
        }
        log(request.method + " for " + std::to_string(client_fd));
        log(request.uri);
        std::string res_name = request.uri;
        if (request.method != "GET") {
            return status_code::FAIL;
        }
        if (cache->contains(res_name)) {
            // we've already tried to get the resource
            log("Found " + res_name + " in cache");
            ResourceInfo *resource = cache->get(res_name);
            if (resource->status != httpparser::HttpResponseParser::ParsingCompleted) {
                resource->subscribers.insert(Subscriber(selected, client_fd, &clients->at(client_fd)));
            }
            log("It's size : " + std::to_string(resource->current_length));
            clients->at(client_fd).res_name = res_name;
            clients->at(client_fd).message_queue.clear();
            if (!resource->parts.empty()) {
                selected->addFd(client_fd, io::SelectData::WRITE);
            }
            log("Got " + std::to_string(clients->at(client_fd).message_queue.size()) + " chunks of resource");
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
                cache->get(res_name)->subscribers.insert(Subscriber(selected, client_fd, &clients->at(client_fd)));
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

    void ProxyWorker::notifySubscribers(const std::string &res_name, ResourceInfo *resource) {
        if (resource->parts.empty()) return;
        if (resource->parts.size() == 1) {
            log("Count of subscribers : " + std::to_string(resource->subscribers.size()));
        }
        for (auto subscriber: resource->subscribers) {
            subscriber.client->res_name = res_name;
            subscriber.selected->addFd(subscriber.fd, io::SelectData::WRITE);
        }
    }

    int ProxyWorker::readServerResponse(int server_fd, io::Message *new_part) {
        auto res_name = servers->at(server_fd).res_name;
        if (!cache->contains(res_name)) {
            logError("Response with out name in cache");
            cache->put(res_name, new ResourceInfo);
        }
//        log("Received " + std::to_string(new_part->len) + " bytes");
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
            notifySubscribers(res_name, resource);
            return status_code::SUCCESS;
        } else if (resource->content_length == resource->current_length && resource->content_length > 0) {
            notifySubscribers(res_name, resource);
            resource->status = httpparser::HttpResponseParser::ParsingCompleted;
            delete resource->full_data;
            resource->full_data = nullptr;
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
        notifySubscribers(res_name, resource);
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
        return status_code::SUCCESS;
    }

    int ProxyWorker::getNoticeFd() const {
        return notice_fd;
    }
}
