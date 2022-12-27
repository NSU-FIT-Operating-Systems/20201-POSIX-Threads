#include "proxy_main.h"

#include <cassert>
#include <cstring>
#include <unistd.h>
#include <csignal>
#include <sys/socket.h>
#include "sys/eventfd.h"
#include <netinet/in.h>

#include "status_code.h"
#include "utils/socket_operations.h"
#include "utils/log.h"

#define TERMINATE_CMD "stop"

namespace worker_thread_proxy {
    static const int MAX_LISTEN_QUEUE_SIZE = 500;
    int signal_pipe[2];

    HttpProxy::HttpProxy(size_t worker_threads_count) {
        assert(worker_threads_count >= 1);
        this->worker_threads_count = worker_threads_count;
        selected = new io::SelectData();
        cache = new aiwannafly::MapCache<Resource>();
        workers = std::vector<Worker*>();
    }

    HttpProxy::~HttpProxy() {
        delete selected;
        delete cache;
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

    void doNothing(int sig) {}

    int initSignalHandlers() {
        int return_value = pipe(signal_pipe);
        if (return_value == status_code::FAIL) {
            return status_code::FAIL;
        }
//        signal(SIGINT, sendTerminate);
//        signal(SIGTERM, sendTerminate);
        signal(SIGPIPE, doNothing);
        return status_code::SUCCESS;
    }

    void *launchWorker(void * arg) {
        auto worker = (ProxyWorker *) arg;
        long code = worker->run();
        return (void *) code;
    }

    int HttpProxy::launchWorkerThreads() {
        assert(worker_threads_count >= 1);
        for (size_t i = 0; i < worker_threads_count; i++) {
            auto worker = new Worker();
            int notice_fd = eventfd(0, 0);
            worker->runner = new ProxyWorker(notice_fd, signal_pipe[io::READ_PIPE_END], cache);
            int code = pthread_create(&worker->tid, nullptr, launchWorker, worker->runner);
            if (code < 0) {
                return status_code::FAIL;
            }
            workers.push_back(worker);
        }
        return status_code::SUCCESS;
    }

    void HttpProxy::run(int port) {
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
        ret_val = launchWorkerThreads();
        if (ret_val == status_code::FAIL) {
            logErrorWithErrno("Could launch worker threads");
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
        while (true) {
            ret_val = select(selected->getMaxFd() + 1, selected->getReadSet(), nullptr,
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
            if (FD_ISSET(proxy_socket, selected->getReadSet())) {
                log("Handle new connection");
                ret_val = acceptNewClient();
                if (ret_val == status_code::FAIL) {
                    logErrorWithErrno("Failed to accept new connection");
                    break;
                }
            }
        }
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
        int notice_fd = workers.at(current_worker_id)->runner->getNoticeFd();
        current_worker_id++;
        if (current_worker_id == worker_threads_count) {
            current_worker_id = 0;
        }
        assert(notice_fd > 0);
        int code = eventfd_write(notice_fd, new_client_fd);
        if (code < 0) {
            return status_code::FAIL;
        }
        log("Sent fd " + std::to_string(new_client_fd) + " to a worker thread");
        return status_code::SUCCESS;
    }

    void HttpProxy::freeResources() {
        log("Shutdown...");
        for (auto worker: workers) {
            int code = pthread_join(worker->tid, nullptr);
            if (code < 0) {
                logErrorWithErrno("Error in pthread_join()");
            }
            delete worker;
        }
        cache->clear();
        int ret_val = close(proxy_socket);
        if (ret_val == status_code::FAIL) {
            logErrorWithErrno("Error in close(proxy_socket)");
        }
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
}
