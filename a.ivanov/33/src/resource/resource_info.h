#ifndef PTHREAD_HTTP_PROXY_SIMPLE_RESOURCE_H
#define PTHREAD_HTTP_PROXY_SIMPLE_RESOURCE_H

#include <cassert>
#include <vector>

#include "resource_subscriber.h"
#include "../sync/threadsafe_list.h"
#include "../utils/io_operations.h"
#include "../utils/select_data.h"
#include "../../httpparser/src/httpparser/httpresponseparser.h"
#include "../proxy_worker/peers.h"

namespace worker_thread_proxy {

    typedef struct ResourceInfo {
    public:
        httpparser::HttpResponseParser::ParseResult status = httpparser::HttpResponseParser::ParsingIncompleted;
        ThreadsafeList<io::Message*> parts = ThreadsafeList<io::Message*>();
        ThreadsafeList<Subscriber> subscribers = ThreadsafeList<Subscriber>();
        io::Message *full_data = nullptr;
        size_t cur_length = 0;
        size_t content_length = 0;
        bool free_messages = true;
        bool has_content_length = false;
        // vector of socket descriptors of clients who wait for the resource

        ResourceInfo() = default;

        ~ResourceInfo() {
            delete full_data;
            if (free_messages) {
                for (auto part : parts) {
                    delete part;
                }
            }
        }

    } ResourceInfo;
}

#endif //PTHREAD_HTTP_PROXY_SIMPLE_RESOURCE_H
