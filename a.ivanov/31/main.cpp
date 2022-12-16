#include <iostream>

#include "single_thread_proxy.h"

#define USAGE_GUIDE "usage: ./prog <proxy_port>"

namespace {
    const int REQUIRED_ARGC = 1 + 1;

    typedef struct args_t {
        bool valid;
        int proxy_port;
        bool print_allowed;
    } args_t;

    bool extract_int(const char *buf, int *num) {
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

    args_t parse_args(int argc, char *argv[]) {
        args_t result;
        result.valid = false;
        if (argc < REQUIRED_ARGC) {
            return result;
        }
        bool extracted = extract_int(argv[1], &result.proxy_port);
        if (!extracted) {
            return result;
        }
        result.print_allowed = false;
        if (argc == REQUIRED_ARGC + 1) {
            if (strcmp(argv[2], "-p") == 0) {
                result.print_allowed = true;
            }
        }
        result.valid = true;
        return result;
    }
}

int main(int argc, char *argv[]) {
    args_t args = parse_args(argc, argv);
    if (!args.valid) {
        fprintf(stderr, "%s\n", USAGE_GUIDE);
        return EXIT_FAILURE;
    }
    proxy *p = new single_thread_proxy::http_proxy();
    p->run(args.proxy_port);
    delete p;
    return EXIT_SUCCESS;
}
