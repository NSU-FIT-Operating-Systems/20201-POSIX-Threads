#ifndef WORKER_THREADS_PROXY_RESOURCE_SUBSCRIBER_H
#define WORKER_THREADS_PROXY_RESOURCE_SUBSCRIBER_H

#include "../utils/select_data.h"
#include "../proxy_worker/peers.h"

namespace worker_thread_proxy {
    typedef struct Subscriber {
        io::SelectData *selected;
        int fd;

        Subscriber(io::SelectData *selected, int fd) {
            assert(selected);
            this->selected = selected;
            this->fd = fd;
        }

        bool operator<(const Subscriber& sub2) const {
            return fd < sub2.fd;
            //Return true if this is less than loc2
        }
    } Subscriber;
}

#endif //WORKER_THREADS_PROXY_RESOURCE_SUBSCRIBER_H
