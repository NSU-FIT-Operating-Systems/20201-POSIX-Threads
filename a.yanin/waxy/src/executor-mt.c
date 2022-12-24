#include "executor.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <common/executor/thread-pool.h>

enum {
    DEFAULT_THREAD_POOL_SIZE = 4,
};

static error_t *parse_unsigned_integer(char const *buf, size_t len, unsigned long long *result) {
    assert(buf != NULL);
    assert(result != NULL);
    assert(buf[len] == '\0');

    error_t *err = NULL;

    err = error_wrap("Expected an integer", OK_IF(len != 0));
    if (err) return err;

    for (char const *c = buf; *c != '\0'; ++c) {
        if (*c == ' ') {
            continue;
        }

        err = error_wrap("Expected a non-negative integer", OK_IF(*c != '-'));
        if (err) return err;

        break;
    }

    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(buf, &end, 10);

    if (end != buf + len) {
        err = error_wrap("The integer was too large", OK_IF(errno != ERANGE));
        if (err) return err;

        return error_from_cstr("Malformed input: expected an integer", NULL);
    }

    *result = parsed;

    return err;
}

static error_t *parse_size(char const *buf, size_t len, size_t *result) {
    assert(buf != NULL);
    assert(result != NULL);
    assert(buf[len] == '\0');

    error_t *err = NULL;

    unsigned long long ull_result = 0;
    err = parse_unsigned_integer(buf, len, &ull_result);
    if (err) return err;

    err = error_wrap("The integer was too large",
        OK_IF((uintmax_t) ull_result <= (uintmax_t) SIZE_MAX));
    if (err) return err;

    *result = ull_result;

    return err;
}

static size_t get_thread_pool_size(void) {
    char const *env = getenv("WAXY_THREAD_POOL_SIZE");
    size_t size = DEFAULT_THREAD_POOL_SIZE;

    if (env != NULL) {
        error_t *err = error_wrap("Could not parse the value of WAXY_THREAD_POOL_SIZE",
            parse_size(env, strlen(env), &size));

        if (!err) {
            err = error_wrap("WAXY_THREAD_POOL_SIZE must be positive", OK_IF(size > 0));
        }

        if (err) {
            error_log_free(&err, LOG_WARN, ERROR_VERBOSITY_SOURCE_CHAIN);
            log_printf(LOG_INFO, "Defaulting to %d", DEFAULT_THREAD_POOL_SIZE);
            size = DEFAULT_THREAD_POOL_SIZE;
        }
    }

    return size;
}

error_t *create_default_executor(executor_t **result) {
    error_t *err = NULL;

    executor_thread_pool_t *executor = NULL;
    size_t thread_pool_size = get_thread_pool_size();
    err = executor_thread_pool_new("Waxy", thread_pool_size, &executor);

    if (err) {
        return err;
    }

    log_printf(LOG_INFO, "Started a thread pool with %zu threads", thread_pool_size);
    *result = (executor_t *) executor;

    return err;
}
