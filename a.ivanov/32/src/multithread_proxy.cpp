#include <unistd.h>
#include <csignal>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <cassert>
#include <sys/eventfd.h>

#include "../httpparser/src/httpparser/httprequestparser.h"

#include "multithread_proxy.h"
#include "status_code.h"
#include "utils/socket_operations.h"
#include "utils/log.h"
#include "resource_manager/caching_resource_manager.h"
#include "client/client.h"


#define TERMINATE_CMD "stop"

namespace multithread_proxy {
    static const int MAX_LISTEN_QUEUE_SIZE = 500;

    void doNothing(int) {}

    void HttpProxy::run(int port) {
        int ret_val = initAndBindProxySocket(port);
        if (ret_val == status_code::FAIL) {
            logError("Could not init and bind proxy socket");
            return;
        }
        signal(SIGPIPE, doNothing);
        ret_val = listen(proxy_socket, MAX_LISTEN_QUEUE_SIZE);
        if (ret_val == status_code::FAIL) {
            logErrorWithErrno("Error in listen");
            ret_val = close(proxy_socket);
            if (ret_val == status_code::FAIL) {
                logErrorWithErrno("Error in close");
            }
        }
        selected->addFd(proxy_socket, io::SelectData::READ);
        log("Running on " + std::to_string(port));
        while (true) {
            ret_val = select(selected->getMaxFd() + 1, selected->getReadSet(),
                             selected->getWriteSet(), nullptr, nullptr);
            if (ret_val < 0) {
                if (errno != EINTR) {
                    logErrorWithErrno("Error in select");
                    freeResources();
                    return;
                }
            }
            assert(ret_val != 0);
            if (FD_ISSET(proxy_socket, selected->getReadSet())) {
                ret_val = acceptNewClient();
                if (ret_val < 0) {
                    logErrorWithErrno("Failed to accept new client");
                    freeResources();
                    return;
                } else {
                    log("Accepted new client");
                }
            }
        }
    }

    void HttpProxy::freeResources() {
        log("Shutdown...");
        log("Free cache of " + std::to_string(cacheSizeBytes()) + " bytes");
        for (const auto &tid : client_tids) {
            int ret_val = pthread_join(tid, nullptr);
            if (ret_val < 0) {
                logErrorWithErrno("Failed to join client thread");
            }
        }
        log("Joined clients");
        resource_manager->clear();
        int ret_val = close(proxy_socket);
        if (ret_val == status_code::FAIL) {
            logErrorWithErrno("Error in close(proxy_socket)");
        }
    }

    HttpProxy::HttpProxy(bool is_print_allowed) {
        resource_manager = new CachingResourceManager();
        selected = new io::SelectData();
    }

    HttpProxy::~HttpProxy() {
        delete resource_manager;
        delete selected;
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

    size_t HttpProxy::cacheSizeBytes() {
        return 0;
    }

    int HttpProxy::acceptNewClient() {
        int new_client_fd = accept(proxy_socket, nullptr, nullptr);
        if (new_client_fd == status_code::FAIL) {
            if (errno != EAGAIN) {
                perror("[PROXY] Error in accept. Shutdown server...");
            }
            return status_code::FAIL;
        }
        auto *arg = new ClientArgs;
        arg->rm = resource_manager;
        arg->fd = new_client_fd;
        pthread_t tid;
        int code = pthread_create(&tid, nullptr, RunNewClient, arg);
        if (code < 0) {
            close(new_client_fd);
            return status_code::FAIL;
        }
        client_tids.push_back(tid);
        return status_code::SUCCESS;
    }
}
