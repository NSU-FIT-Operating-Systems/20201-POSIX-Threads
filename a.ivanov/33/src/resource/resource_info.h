#ifndef PTHREAD_HTTP_PROXY_SIMPLE_RESOURCE_H
#define PTHREAD_HTTP_PROXY_SIMPLE_RESOURCE_H

#include <cassert>
#include <set>
#include <vector>

#include "../sync/threadsafe_list.h"
#include "../utils/io_operations.h"
#include "../utils/select_data.h"
#include "../../httpparser/src/httpparser/httpresponseparser.h"
#include "../proxy_worker/peers.h"

namespace worker_thread_proxy {

    typedef struct Subscriber {
        io::SelectData *selected;
        int fd;
        ClientInfo *client;

        Subscriber(io::SelectData *selected, int fd, ClientInfo *client) {
            assert(selected);
            assert(client);
            this->selected = selected;
            this->fd = fd;
            this->client = client;
        }

        bool operator<(const Subscriber& sub2) const {
            return fd < sub2.fd;
            //Return true if this is less than loc2
        }
    } Subscriber;

    typedef struct ResourceInfo {
    public:
        httpparser::HttpResponseParser::ParseResult status = httpparser::HttpResponseParser::ParsingIncompleted;
        ThreadsafeList<io::Message> parts = ThreadsafeList<io::Message>();
        io::Message *full_data = nullptr;
        size_t cur_length = 0;
        size_t content_length = 0;
        bool free_messages = true;
        // vector of socket descriptors of clients who wait for the resource
        std::set<Subscriber> subscribers = std::set<Subscriber>();

        ResourceInfo() {
            mutex = new pthread_mutex_t;
            pthread_mutex_init(mutex, nullptr);
        }

        void lock() {
            pthread_mutex_lock(mutex);
        }

        void unlock() {
            pthread_mutex_unlock(mutex);
        }

        ~ResourceInfo() {
            delete full_data;
            if (free_messages) {
                for (auto part : parts) {
                    delete part;
                }
            }
            pthread_mutex_destroy(mutex);
            delete mutex;
        }

    private:
        pthread_mutex_t *mutex;
    } ResourceInfo;
}

#endif //PTHREAD_HTTP_PROXY_SIMPLE_RESOURCE_H
