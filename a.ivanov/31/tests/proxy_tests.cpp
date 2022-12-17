#include "proxy_tests.h"

#include <chrono>
#include <curl/curl.h>
#include <gtest/gtest.h>
#include <string>
#include "pthread.h"

#define USAGE_GUIDE "usage: ./tests <proxy_port>"
#define TEST_FILES_URL "www.ccfit.nsu.ru/~rzheutskiy/test_files"
#define DATA_100BM_URL "www.ccfit.nsu.ru/~rzheutskiy/test_files/100mb.dat"

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

    size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
        ((std::string *) userp)->append((char *) contents, size * nmemb);
        return size * nmemb;
    }

    size_t port;

    void log(const std::string &s) {
        std::cout << "[ TEST     ] " << s << std::endl;
    }

    void log_err(const std::string &s) {
        std::cerr << "[ ERROR    ] " + s + "\n";
    }

    typedef struct download_info {
        pthread_t tid{};
        std::string buffer;
        CURLcode code = CURLE_READ_ERROR;
        bool with_proxy = true;
        std::string url;
    } download_info;

    void get_data(const std::string &url, bool with_proxy, std::string *buffer, CURLcode *res) {
        assert(buffer);
        assert(res);
        CURL *curl;
        curl = curl_easy_init();
        if (curl == nullptr) return;
        curl_easy_setopt(curl, CURLOPT_URL, url.data());
        if (with_proxy) {
            curl_easy_setopt(curl, CURLOPT_PROXY, ("http://localhost:" + std::to_string(port)).data());
        }
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
        *res = curl_easy_perform(curl);
        if (*res != CURLE_OK) {
            log_err(curl_easy_strerror(*res));
        }
        curl_easy_cleanup(curl);
    }

    void *download_start(void *arg) {
        auto *info = (struct download_info *) arg;
        get_data(info->url, info->with_proxy, &info->buffer, &info->code);
        return nullptr;
    }
}

TEST(HTTP_PROXY, BaseTest) {
    CURLcode res;
    std::string read_buffer;
    get_data(TEST_FILES_URL, true, &read_buffer, &res);
    EXPECT_EQ(res, CURLE_OK);
}

//TEST(HTTP_PROXY, SpeedIncreaseTest) {
//    CURLcode res;
//    std::string read_buffer1;
//    auto start = std::chrono::steady_clock::now();
//    get_data(DATA_100BM_URL, true, &read_buffer1, &res);
//    auto end = std::chrono::steady_clock::now();
//    EXPECT_EQ(res, CURLE_OK);
//    size_t millis_first = duration_cast<std::chrono::milliseconds>(end - start).count();
//    std::string read_buffer2;
//    start = std::chrono::steady_clock::now();
//    get_data(DATA_100BM_URL, true, &read_buffer2, &res);
//    end = std::chrono::steady_clock::now();
//    EXPECT_EQ(res, CURLE_OK);
//    size_t millis_second = duration_cast<std::chrono::milliseconds>(end - start).count();
//    EXPECT_TRUE(millis_second * 2 < millis_first);
//    log("Completed " + std::to_string(millis_first / millis_second) + " times faster");
//}

TEST(HTTP_PROXY, MultipleConnectionsTest) {
    size_t conns_count = 5;
    ASSERT_FALSE(conns_count <= 0);
    auto download_segments = std::vector<download_info*>();
    for (size_t i = 0; i < conns_count; i++) {
        auto info = new download_info();
        info->url = DATA_100BM_URL;
        download_segments.push_back(info);
        int code = pthread_create(&info->tid, nullptr, download_start, info);
        ASSERT_FALSE(code < 0);
    }
    for (const auto &info : download_segments) {
        int code = pthread_join(info->tid, nullptr);
        ASSERT_FALSE(code < 0);
    }
    size_t length = download_segments.front()->buffer.size();
    for (const auto &info : download_segments) {
        EXPECT_EQ(info->code, CURLE_OK);
        EXPECT_EQ(length, info->buffer.size());
        delete info;
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

