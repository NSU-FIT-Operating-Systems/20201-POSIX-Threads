#include "proxy_main.h"

#include <cassert>
#include <cstring>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "status_code.h"
#include "utils/socket_operations.h"
#include "utils/log.h"

namespace worker_thread_proxy {
    static const int MAX_LISTEN_QUEUE_SIZE = 500;
    int global_sig_fd = -1;
    size_t global_workers_count = 1;

    HttpProxy::HttpProxy(size_t worker_threads_count) {
        assert(worker_threads_count >= 1);
        this->worker_threads_count = worker_threads_count;
        this->selected = new io::SelectData();
        this->cache = new MapCache<ResourceInfo>();
        this->workers = std::vector<Worker *>();
        this->signal_fd = eventfd(0, EFD_SEMAPHORE);
        assert(this->signal_fd > 0);
        global_sig_fd = signal_fd;
        global_workers_count = worker_threads_count;
    }

    HttpProxy::~HttpProxy() {
        delete selected;
        delete cache;
    }

    void sendTerminate(__attribute__((unused)) int sig) {
        log("Send terminate");
        int code = eventfd_write(global_sig_fd, global_workers_count + 1);
        if (code < 0) {
            logErrorWithErrno("Error in eventfd_write()");
        }
    }

    void doNothing(int) {}

    int initSignalHandlers() {
        struct sigaction term_action{};
        sigemptyset(&term_action.sa_mask);
        term_action.sa_handler = sendTerminate;
        term_action.sa_flags = 0;
        sigaction(SIGINT, nullptr, &term_action);
        sigaction(SIGTERM, nullptr, &term_action);
        signal(SIGPIPE, doNothing);
        return status_code::SUCCESS;
    }

    void *launchWorker(void *arg) {
        auto worker = (ProxyWorker *) arg;
        long code = worker->run();
        return (void *) code;
    }

    int HttpProxy::launchWorkerThreads() {
        assert(worker_threads_count >= 1);
        for (size_t i = 0; i < worker_threads_count; i++) {
            auto worker = new Worker();
            worker->runner = new ProxyWorker(signal_fd, cache);
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
        initSignalHandlers();
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
        selected->addFd(signal_fd, io::SelectData::READ);
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
            if (FD_ISSET(signal_fd, selected->getReadSet())) {
                log("Terminate main thread");
                freeResources();
                return;
            }
            if (FD_ISSET(proxy_socket, selected->getReadSet())) {
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
        auto worker_selected = workers.at(current_worker_id)->runner->getSelected();
        current_worker_id++;
        if (current_worker_id == worker_threads_count) {
            current_worker_id = 0;
        }
        worker_selected->addFd(new_client_fd, io::SelectData::READ);
        return status_code::SUCCESS;
    }

    void HttpProxy::freeResources() {
        log("Shutdown proxy main thread...");
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
