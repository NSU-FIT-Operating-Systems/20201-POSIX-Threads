#ifndef HTTP_CPP_PROXY_MAIN_H
#define HTTP_CPP_PROXY_MAIN_H

#include <cstdlib>
#include <map>
#include <set>
#include <vector>

#include "cache/map_cache.h"
#include "proxy_worker/proxy_worker.h"
#include "proxy.h"
#include "resource/resource_info.h"
#include "utils/select_data.h"
#include "utils/io_operations.h"

#include "../httpparser/src/httpparser/httpresponseparser.h"
#include "../httpparser/src/httpparser/response.h"

namespace worker_thread_proxy {

    typedef struct Worker {
        ProxyWorker *runner;
        pthread_t tid;

        ~Worker() {
            delete runner;
        }
    } Worker;

    class HttpProxy final : public Proxy {
    public:

        explicit HttpProxy(size_t worker_threads_count);

        ~HttpProxy() final;

        void run(int port) final;

    private:

        int initAndBindProxySocket(int port);

        int acceptNewClient();

        void freeResources();

        int launchWorkerThreads();

        size_t worker_threads_count = 1;
        size_t current_worker_id = 0;

        int signal_fd = -1;
        int proxy_socket = 0;
        io::SelectData *selected;
        Cache<ResourceInfo> *cache;
        std::vector<Worker*> workers;
    };
}

#endif //HTTP_CPP_PROXY_MAIN_H
