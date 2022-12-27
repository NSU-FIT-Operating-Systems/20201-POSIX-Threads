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
#include "../resource/simple_resource.h"

#define TERMINATE_CMD "stop"

namespace worker_thread_proxy {
    static const int HTTP_PORT = 80;

    ProxyWorker::ProxyWorker(int notice_fd, int signal_fd, aiwannafly::Cache<Resource> *cache) {
        assert(cache);
        selected = new io::SelectData();
        clients = new std::map<int, ClientInfo>();
        servers = new std::map<int, ServerInfo>();
        resource_names = new std::map<int, std::string>();
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
        delete resource_names;
    }

    int ProxyWorker::run() {
        selected->addFd(notice_fd, io::SelectData::READ);
        selected->addFd(signal_fd, io::SelectData::READ);
        fd_set constant_read_set;
        fd_set constant_write_set;
        while (true) {
            memcpy(&constant_read_set, selected->getReadSet(), sizeof(*selected->getReadSet()));
            memcpy(&constant_write_set, selected->getWriteSet(), sizeof(*selected->getWriteSet()));
            int ret_val = select(selected->getMaxFd() + 1, &constant_read_set, &constant_write_set,
                             nullptr, nullptr);
            if (ret_val == status_code::FAIL || ret_val == status_code::TIMEOUT) {
                if (errno != EINTR && ret_val == status_code::FAIL) {
                    logErrorWithErrno("Error in select");
                    freeResources();
                    return status_code::FAIL;
                }
                if (ret_val == status_code::TIMEOUT) {
                    log("Select timed out. End program.");
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
                cache->get(res_name)->cancel();
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
        if (resource_names->contains(fd)) {
            log("Client got notification from a resource");
            return readResourceNotification(fd);
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

    int ProxyWorker::readResourceNotification(int fd) {
        eventfd_t value;
        int code = eventfd_read(fd, &value);
        if (code < 0) {
            return status_code::FAIL;
        }
        assert(resource_names->contains(fd));
        std::string res_name = resource_names->at(fd);
        auto *resource = cache->get(res_name);
        for (auto &client : *clients) {
            if (client.second.res_name == res_name) {
                log("Client should get " + std::to_string(resource->getParts()->size()) + " parts");
                if (client.second.recv_msg_count < resource->getParts()->size()) {
                    selected->addFd(client.first, io::SelectData::WRITE);
                }
                for (size_t i = client.second.recv_msg_count; i < resource->getParts()->size(); i++) {
                    log("Push new part into a queue");
                    clients->at(client.first).message_queue.push_back(resource->getParts()->at(i));
                }
            }
        }
        return status_code::SUCCESS;
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
                    clients->at(fd).recv_msg_count++;
//                    log("Sent to client " + std::to_string(len) + " bytes");
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
            Resource *resource = cache->get(res_name);
            int res_fd = resource->getNotifyFd();
            selected->addFd(res_fd, io::SelectData::READ);
            (*resource_names)[res_fd] = res_name;
            log("It's size : " + std::to_string(resource->getCurrentLength()));
            clients->at(client_fd).res_name = res_name;
            clients->at(client_fd).message_queue.clear();
            if (resource->getParts()->size() > 0) {
                selected->addFd(client_fd, io::SelectData::WRITE);
            }
            for (size_t i = 0; i < resource->getParts()->size(); i++) {
                clients->at(client_fd).message_queue.push_back(resource->getParts()->at(i));
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
                Resource *resource;
                if (!cache->contains(res_name)) {
                    resource = new SimpleResource();
                    cache->put(res_name, resource);
                } else {
                    resource = cache->get(res_name);
                }
                if (resource == nullptr) {
                    logError("Resource is null");
                    return status_code::FAIL;
                }
                int res_fd = resource->getNotifyFd();
                selected->addFd(res_fd, io::SelectData::READ);
                (*resource_names)[res_fd] = res_name;
                if (servers->contains(sd)) {
                    log("Server res name : " + servers->at(sd).res_name);
                }
                clients->at(client_fd).res_name = res_name;
                servers->at(sd).res_name = res_name;
                log(std::string("Started connecting to " + header.value));
                return ret_val;
            }
        }
        logError("Not found host header");
        return status_code::FAIL;
    }

    int ProxyWorker::notifySubscribers(Resource *resource) {
        size_t post_count = resource->getSubscribesCount();
        if (post_count == 0) post_count = 1;
        int code = eventfd_write(resource->getNotifyFd(), post_count);
        if (code == 0) {
            log("Sent notification to " + std::to_string(post_count) + " clients");
        }
        return code;
    }

    int ProxyWorker::readServerResponse(int server_fd, io::Message *new_part) {
        auto res_name = servers->at(server_fd).res_name;
        if (!cache->contains(res_name)) {
            logError("Response with out name in cache");
            cache->put(res_name, new SimpleResource);
        }
//        log("Received " + std::to_string(new_part->len) + " bytes");
        auto resource = cache->get(res_name);
        resource->getParts()->add(new_part);
        io::Message *full_msg = new_part;
        if (resource->getContentLength() == 0) {
            if (resource->getData() != nullptr) {
                full_msg = resource->getData();
                bool added = io::AppendMsg(resource->getData(), new_part);
                if (!added) {
                    return status_code::FAIL;
                }
            } else {
                resource->updateData(io::copy(new_part));
            }
        }
        resource->setCurrentLength(resource->getCurrentLength() + new_part->len);
//        log("Server " + std::to_string(fd) + " received " + std::to_string(resource->getCurrentLength()) +
//            "/" + std::to_string(resource->getContentLength()) + " bytes");
        if (resource->getContentLength() > resource->getCurrentLength()) {
            return notifySubscribers(resource);
        } else if (resource->getContentLength() == resource->getCurrentLength()
                   && resource->getCurrentLength() > 0) {
            resource->setStatus(COMPLETED);
            delete resource->getData();
            resource->updateData(nullptr);
            notifySubscribers(resource);
            return status_code::SUCCESS;
        }
        httpparser::Response response;
        httpparser::HttpResponseParser parser;
        httpparser::HttpResponseParser::ParseResult res = parser.parse(response, full_msg->data,
                                                                       full_msg->data + full_msg->len);
        log("Parsed response");
        if (res == httpparser::HttpResponseParser::ParsingError) {
            logError("Failed to parse response of " + res_name);
            return status_code::FAIL;
        }
        notifySubscribers(resource);
        if (res == httpparser::HttpResponseParser::ParsingIncompleted) {
            auto content_length_header = std::find_if(response.headers.begin(), response.headers.end(),
                                                      [&](const httpparser::Response::HeaderItem &item) {
                                                          return item.name == "Content-Length";
                                                      });
            if (content_length_header != response.headers.end()) {
                size_t content_length = std::stoul(content_length_header->value);
                resource->setContentLength(content_length);
                assert(full_msg->len > response.content.size());
                size_t sub = full_msg->len - response.content.size();
                if (sub > 0) {
                    resource->setContentLength(content_length + sub);
                }
                log("Content-Length : " + std::to_string(resource->getContentLength()));
            }
            log("Response of is not complete, it's current length: " +
                std::to_string(full_msg->len));
            assert(resource->getStatus() == INCOMPLETED);
            return status_code::SUCCESS;
        }
        log("Parsed response of " + res_name + std::string(" code: ") + std::to_string(response.statusCode));
        resource->setStatus(COMPLETED);
        log("Full bytes length : " + std::to_string(resource->getData()->len));
        log("Full parts count : " + std::to_string(resource->getParts()->size()));
        delete resource->getData();
        resource->updateData(nullptr);
        if (response.statusCode != 200) {
            log("Status code is not 200, not store " + res_name + " in cache");
        }
        return status_code::SUCCESS;
    }

    int ProxyWorker::getNoticeFd() const {
        return notice_fd;
    }
}
