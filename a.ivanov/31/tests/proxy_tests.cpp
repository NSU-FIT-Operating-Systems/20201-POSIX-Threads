#include "proxy_tests.h"

#include <curl/curl.h>
#include <gtest/gtest.h>
#include <iostream>
#include <string>

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string *) userp)->append((char *) contents, size * nmemb);
    return size * nmemb;
}

static size_t port;

TEST(HTTP_PROXY, BaseTest) {
    CURL *curl;
    CURLcode res;
    std::string readBuffer;
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "www.ccfit.nsu.ru/~rzheutskiy/test_files");
        curl_easy_setopt(curl, CURLOPT_PROXY, ("http://localhost:" + std::to_string(port)).data());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        res = curl_easy_perform(curl);
        EXPECT_EQ(res, CURLE_OK);
        curl_easy_cleanup(curl);
        std::cout << readBuffer << std::endl;
    }
}

#define USAGE_GUIDE "usage: ./tests <proxy_port>"

namespace {
    const int REQUIRED_ARGC = 1 + 1;

    typedef struct args_t {
        bool valid;
        int proxy_port;
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
        result.valid = true;
        return result;
    }
}

int run_all_tests(int argc, char *argv[]) {
    args_t args = parse_args(argc, argv);
    if (!args.valid) {
        fprintf(stderr, "%s\n", USAGE_GUIDE);
        return -1;
    }
    port = args.proxy_port;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

