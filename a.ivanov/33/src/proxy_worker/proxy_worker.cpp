#include "proxy_worker.h"

#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../status_code.h"
#include "../utils/socket_operations.h"
#include "../utils/log.h"

#include "../../httpparser/src/httpparser/request.h"
#include "../../httpparser/src/httpparser/httprequestparser.h"

namespace worker_thread_proxy {
    using namespace httpparser;

    static const int HTTP_PORT = 80;

    ProxyWorker::ProxyWorker(int signal_fd, Cache<ResourceInfo> *cache) {
        assert(cache);
        this->selected = new io::SelectData();
        this->clients = new std::map<int, ClientInfo *>();
        this->servers = new std::map<int, ServerInfo *>();
        this->signal_fd = signal_fd;
        this->cache = cache;
        this->DNS_map = new std::map<std::string, struct hostent *>();
    }

    ProxyWorker::~ProxyWorker() {
        delete selected;
        delete clients;
        delete servers;
        delete cache;
        delete DNS_map;
    }

    io::SelectData *ProxyWorker::getSelected() const {
        return selected;
    }

    int ProxyWorker::run() {
        selected->addFd(signal_fd, io::SelectData::READ);
        fd_set constant_read_set;
        fd_set constant_write_set;
        while (true) {
            struct timeval timeout = {
                    .tv_sec = 0,
                    .tv_usec = 200 * 1000
            }; // timeout is required to prevent situation when new resource is
            // available, but client did not set it's fd into WRITE mode
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
            } else if (ret_val == status_code::TIMEOUT) {
                // here we check resources updates
                for (int fd = 2; fd <= selected->getMaxFd(); ++fd) {
                    if (clients->contains(fd)) {
                        lootNewResourceParts(fd, true);
                    }
                }
            }
            int desc_ready = ret_val;
            for (int fd = 2; fd <= selected->getMaxFd() && desc_ready > 0; ++fd) {
                if (FD_ISSET(fd, &constant_read_set)) {
                    desc_ready -= 1;
                    //log("Got message");
                    ret_val = readMessageFrom(fd);
                    if (ret_val == status_code::TERMINATE) {
                        logError("Terminate");
                        freeResources();
                        return status_code::TERMINATE;
                    } else if (ret_val == status_code::FAIL) {
                        logError("Failed to handle a message");
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
        log("Shutdown worker thread...");
        for (int fd = 2; fd <= selected->getMaxFd(); fd++) {
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
        if (servers->contains(fd)) {
            for (const auto &msg: servers->at(fd)->msg_queue) {
                delete msg;
            }
            delete servers->at(fd);
            servers->erase(fd);
        }
        if (clients->contains(fd)) {
            logError("Client " + std::to_string(fd) + " disconnects");
            auto res_name = clients->at(fd)->res_name;
            if (cache->contains(res_name)) {
                ResourceInfo *resource = cache->get(res_name);
                for (auto it = resource->subscribers.begin(); it != resource->subscribers.end(); it++) {
                    if (it->fd == fd) {
                        resource->subscribers.erase(it);
                        break;
                    }
                }
            }
            delete clients->at(fd);
            clients->erase(fd);
        }
        selected->remove_fd(fd, io::SelectData::READ);
        selected->remove_fd(fd, io::SelectData::WRITE);
        return close(fd);
    }

    int ProxyWorker::readMessageFrom(int fd) {
        assert(fd > 0);
        if (fd == signal_fd) {
            return status_code::TERMINATE;
        }
        io::Message *message = io::read_all(fd);
        if (message == nullptr) {
            return status_code::FAIL;
        }
        if (message->len == 0) {
            int ret_val = closeConnection(fd);
            if (ret_val == status_code::FAIL) {
                logErrorWithErrno("Error in close connection");
                return status_code::SUCCESS;
            }
            delete message;
            return ret_val;
        }
        if (servers->contains(fd)) {
            int ret_val = readServerResponse(fd, message);
            if (ret_val == status_code::FAIL) {
                delete message;
            }
            return ret_val;
        }
        if (!clients->contains(fd)) {
            (*clients)[fd] = new ClientInfo();
        }
        int ret_val = readClientRequest(fd, message);
        return ret_val;
    }

    void ProxyWorker::lootNewResourceParts(int fd, bool reset) {
        if (!clients->contains(fd)) return;
        ClientInfo *client = clients->at(fd);
        size_t msg_count = client->msg_queue.size();
        if (cache->contains(client->res_name) && msg_count == 0) {
            auto res = cache->get(client->res_name);
            std::copy(res->parts.begin() + (int) client->recv_msg_count,
                      res->parts.end(), std::back_inserter(client->msg_queue));
        }
        msg_count = client->msg_queue.size();
        if (reset && msg_count > 0) {
            selected->addFd(fd, io::SelectData::WRITE);
        }
    }

    int ProxyWorker::writeMessageTo(int fd) {
        selected->remove_fd(fd, io::SelectData::WRITE);
        if (servers->contains(fd)) {
            assert(!clients->contains(fd));
            ServerInfo *server = servers->at(fd);
            if (!server->connected) {
                int ret_val = finishConnectToServer(fd);
                if (ret_val == status_code::FAIL) {
                    logErrorWithErrno("Failed to connect to host");
                } else {
                    if (!server->msg_queue.empty()) {
                        selected->addFd(fd, io::SelectData::WRITE);
                    }
                }
            } else {
                size_t msg_count = server->msg_queue.size();
                if (msg_count >= 1) {
                    auto *msg = server->msg_queue.front();
                    server->msg_queue.erase(server->msg_queue.begin());
                    bool written = io::WriteAll(fd, msg);
                    log("Sent request to a server " + std::to_string(fd));
                    delete msg;
                    msg_count--;
                    if (msg_count > 0) {
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
            ClientInfo *client = clients->at(fd);
            lootNewResourceParts(fd, false);
            size_t msg_count = client->msg_queue.size();
            if (msg_count >= 1) {
                auto msg = client->msg_queue.front();
                client->msg_queue.erase(client->msg_queue.begin());
                bool written = io::WriteAll(fd, msg);
                if (!cache->contains(client->res_name)) {
                    delete msg;
                }
                msg_count--;
                if (msg_count > 0) {
                    selected->addFd(fd, io::SelectData::WRITE);
                }
                client->recv_msg_count++;
                if (!written) {
                    client->recv_msg_count--;
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
                (*servers)[sd] = new ServerInfo();
                (*servers)[sd]->connected = false;
                return sd;
            }
            return status_code::FAIL;
        }
        selected->addFd(sd, io::SelectData::READ);
        (*servers)[sd] = new ServerInfo();
        (*servers)[sd]->connected = true;
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
        servers->at(fd)->connected = true;
        return status_code::SUCCESS;
    }

    int ProxyWorker::readClientRequest(int client_fd, io::Message *request_message) {
        assert(request_message);
        assert(clients->contains(client_fd));
        ClientInfo *client = clients->at(client_fd);
        client->msg_queue.clear();
        client->recv_msg_count = 0;
        Request request;
        HttpRequestParser parser;
        HttpRequestParser::ParseResult parse_res = parser.parse(request,
                                                          request_message->data,
                                                          request_message->data + request_message->len);
        if (parse_res != HttpRequestParser::ParsingCompleted) {
            delete request_message;
            return status_code::FAIL;
        }
        log(request.method + " for " + std::to_string(client_fd));
        log(request.uri);
        std::string res_name = request.uri;
        client->res_name = res_name;
        if (request.method != "GET") {
            // working with only GET
            delete request_message;
            return status_code::SUCCESS;
        }
        cache->lock();
        if (cache->contains(res_name)) {
            // we've already tried to get the resource
            log("Found " + res_name + " in cache");
            ResourceInfo *res = cache->get(res_name);
            if (res->status != HttpResponseParser::ParsingCompleted) {
                // subscribe if resource is still active
                res->subscribers.push_back(Subscriber(selected, client_fd));
            }
            log("It's size : " + std::to_string(res->cur_length));
            if (!res->parts.empty()) {
                selected->addFd(client_fd, io::SelectData::WRITE);
            }
            log("Found " + std::to_string(res->parts.size()) + " chunks of resource");
            delete request_message;
            cache->unlock();
            return status_code::SUCCESS;
        }
        for (const httpparser::Request::HeaderItem &header: request.headers) {
            log(header.name + std::string(" : ") + header.value);
            if (header.name == "Host") {
                int ret_val = beginConnectToServer(header.value, HTTP_PORT);
                if (ret_val == status_code::FAIL) {
                    logErrorWithErrno("Failed to connect to remote");
                    delete request_message;
                    return status_code::FAIL;
                }
                int server_fd = ret_val;
                assert(servers->contains(server_fd));
                ServerInfo *server = servers->at(server_fd);
                server->res_name = res_name;
                server->msg_queue.push_back(request_message);
                if (servers->contains(server_fd)) {
                    log("Server res name : " + server->res_name);
                }
                if (!cache->contains(res_name)) {
                    cache->put(res_name, new ResourceInfo());
                    log(std::string("Started connecting to " + header.value));
                }
                cache->get(res_name)->subscribers.push_back(Subscriber(selected, client_fd));
                cache->unlock();
                return ret_val;
            }
        }
        logError("Not found host header");
        delete request_message;
        return status_code::FAIL;
    }

    void ProxyWorker::notifySubscribers(const std::string &res_name, ResourceInfo *resource) {
        if (resource->parts.empty()) return;
        if (resource->parts.size() == 1) {
            log("Count of subscribers : " + std::to_string(resource->subscribers.size()));
        }
        for (auto subscriber: resource->subscribers) {
            subscriber.selected->addFd(subscriber.fd, io::SelectData::WRITE);
        }
    }

    int ProxyWorker::readServerResponse(int server_fd, io::Message *new_part) {
        auto res_name = servers->at(server_fd)->res_name;
        if (!cache->contains(res_name)) {
            logError("Response with out name in cache");
            cache->put(res_name, new ResourceInfo);
        }
        auto res = cache->get(res_name);
        if (res->status == HttpResponseParser::ParsingCompleted) {
            delete new_part;
            return status_code::SUCCESS;
        }
        res->parts.push_back(new_part);
        io::Message *full_msg = new_part;
        if (!res->has_content_length) {
            if (res->full_data != nullptr) {
                full_msg = res->full_data;
                bool added = io::AppendMsg(res->full_data, new_part);
                if (!added) {
                    return status_code::FAIL;
                }
            } else {
                res->full_data = io::copy(new_part);
            }
        }
        res->cur_length += new_part->len;
        assert(!(res->has_content_length && res->cur_length > res->content_length));
        if (res->has_content_length && res->content_length > res->cur_length) {
            notifySubscribers(res_name, res);
            return status_code::SUCCESS;
        } else if (res->has_content_length && res->content_length == res->cur_length) {
            notifySubscribers(res_name, res);
            res->status = HttpResponseParser::ParsingCompleted;
            delete res->full_data;
            res->full_data = nullptr;
            return status_code::SUCCESS;
        }
        Response response;
        HttpResponseParser parser;
        HttpResponseParser::ParseResult parse_res = parser.parse(response, full_msg->data,
                                                                 full_msg->data + full_msg->len);
        if (parse_res == HttpResponseParser::ParsingError) {
            logError("Failed to parse response of " + res_name);
            return status_code::FAIL;
        }
        notifySubscribers(res_name, res);
        if (parse_res == httpparser::HttpResponseParser::ParsingIncompleted) {
            auto content_length_header = std::find_if(response.headers.begin(), response.headers.end(),
                                                      [&](const httpparser::Response::HeaderItem &item) {
                                                          return item.name == "Content-Length";
                                                      });
            if (content_length_header != response.headers.end()) {
                size_t content_length = std::stoul(content_length_header->value);
                res->content_length = content_length;
                assert(full_msg->len > response.content.size());
                size_t sub = full_msg->len - response.content.size();
                assert(sub >= 0);
                res->content_length += sub;
                res->has_content_length = true;
            }
            assert(res->status == HttpResponseParser::ParsingIncompleted);
            return status_code::SUCCESS;
        }
        // ParsingSuccess
        log("Parsed response of " + res_name + std::string(" code: ") + std::to_string(response.statusCode));
        res->status = HttpResponseParser::ParsingCompleted;
        log("Full bytes length : " + std::to_string(res->full_data->len));
        log("Full parts count : " + std::to_string(res->parts.size()));
        delete res->full_data;
        res->full_data = nullptr;
        return status_code::SUCCESS;
    }
}
