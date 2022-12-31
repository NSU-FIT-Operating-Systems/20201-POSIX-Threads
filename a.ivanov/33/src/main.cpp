#include <string>

#include "proxy_main.h"

#define USAGE_GUIDE "usage: ./prog <proxy_port> <worker_threads_count>"

namespace {
    const int REQUIRED_ARGC = 1 + 1 + 1;

    typedef struct args_t {
        bool valid;
        int proxy_port;
        size_t worker_threads_count;
    } args_t;

    bool ExtractInt(const char *buf, int *num) {
        if (nullptr == buf || num == nullptr) {
            return false;
        }
        char *end_ptr = nullptr;
        *num = (int) strtol(buf, &end_ptr, 10);
        if (buf + strlen(buf) > end_ptr) {
            return false;
        }
        return true;
    }

    bool ExtractUint(const char *buf, size_t *num) {
        if (nullptr == buf || num == nullptr) {
            return false;
        }
        char *end_ptr = nullptr;
        *num = (int) strtoul(buf, &end_ptr, 10);
        if (buf + strlen(buf) > end_ptr) {
            return false;
        }
        return true;
    }

    args_t ParseArgs(int argc, char *argv[]) {
        args_t result;
        result.valid = false;
        if (argc < REQUIRED_ARGC) {
            return result;
        }
        bool extracted = ExtractInt(argv[1], &result.proxy_port);
        if (!extracted) {
            return result;
        }
        extracted = ExtractUint(argv[2], &result.worker_threads_count);
        if (!extracted) {
            return result;
        }
        result.valid = true;
        return result;
    }
}

int main(int argc, char *argv[]) {
    args_t args = ParseArgs(argc, argv);
    if (!args.valid) {
        fprintf(stderr, "%s\n", USAGE_GUIDE);
        return EXIT_FAILURE;
    }
    Proxy *p = new worker_thread_proxy::HttpProxy(args.worker_threads_count);
    p->run(args.proxy_port);
    delete p;
    pthread_exit(nullptr);
}
