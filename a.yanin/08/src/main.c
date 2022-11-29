#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

#include <common/log/log.h>
#include <common/posix/time.h>

#include "error.h"

enum {
    TERMS_PER_UNIT = 4,
};

typedef struct {
    size_t iteration_count;
    size_t worker_idx;
    size_t worker_count;
    double *output;
} worker_param_t;

typedef union {
    double partial_sum;
    void *opaque_result;
} result_t;

static bool const can_return_double = sizeof(double) <= sizeof(void *);

static void finish_worker_thread(worker_param_t *worker_params, double result) {
    if (can_return_double) {
        pthread_exit((result_t) { .partial_sum = result }.opaque_result);
    }

    worker_params->output[worker_params->worker_idx] = result;
    pthread_exit(0);
}

static void print_usage(void) {
    fputs("Usage: pi [<thread count>] [<iteration count>]\n", stderr);
}

static void *worker_thread(void *worker_params_opaque) {
    assert(worker_params_opaque != NULL);

    worker_param_t *worker_params = worker_params_opaque;
    size_t iteration_count = worker_params->iteration_count;
    size_t worker_count = worker_params->worker_count;
    size_t worker_idx = worker_params->worker_idx;
    double partial_sum = 0;

    for (size_t i = TERMS_PER_UNIT * worker_idx;
            i < iteration_count;
            i += TERMS_PER_UNIT * worker_count) {
        for (size_t j = 0; j < TERMS_PER_UNIT && i + j < iteration_count; ++j) {
            size_t term_n = i + j;
            double sign = term_n % 2 == 0 ? 1 : -1;
            double term = sign / (term_n * 2 + 1);
            partial_sum += term;
        }
    }

    finish_worker_thread(worker_params, partial_sum);

    return 0;
}

static err_t parse_unsigned_integer(char const *buf, size_t len, unsigned long long *result) {
    assert(buf != NULL);
    assert(result != NULL);
    assert(buf[len] == '\0');

    err_t error = OK;

    if (ERR_FAILED(error = ERR((bool)(len != 0), "expected an integer"))) return error;

    for (char const *c = buf; *c != '\0'; ++c) {
        if (*c == ' ') {
            continue;
        }

        error = ERR((bool)(*c != '-'), "expected a non-negative integer");
        if (ERR_FAILED(error)) return error;

        break;
    }

    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(buf, &end, 10);

    if (end != buf + len) {
        error = ERR((bool)(errno == ERANGE), "the integer was too large");
        if (ERR_FAILED(error)) return error;

        return ERR(false, "malformed input: expected an integer");
    }

    *result = parsed;

    return error;
}

static err_t parse_size(char const *buf, size_t len, size_t *result) {
    assert(buf != NULL);
    assert(result != NULL);
    assert(buf[len] == '\0');

    unsigned long long ull_result = 0;
    err_t error = ERR(parse_unsigned_integer(buf, len, &ull_result), "failed to parse an integer");
    if (ERR_FAILED(error)) return error;

    error = ERR((bool)((uintmax_t) ull_result <= (uintmax_t) SIZE_MAX),
        "the integer was too large");
    if (ERR_FAILED(error)) return error;

    *result = ull_result;

    return error;
}

int main(int argc, char *argv[]) {
    if (!(argc >= 1 && argc <= 3)) {
        print_usage();

        return 1;
    }

    err_t error = OK;
    size_t worker_count = 4;
    size_t iteration_count = 1'000'000;

    if (argc >= 2) {
        error = ERR(parse_size(argv[1], strlen(argv[1]), &worker_count),
            "failed to parse the worker thread count");
        if (ERR_FAILED(error)) goto arg_parse_fail;

        error = ERR((bool)(worker_count > 0), "the worker count cannot be zero");
        if (ERR_FAILED(error)) goto arg_parse_fail;
    }

    if (argc >= 3) {
        error = ERR(parse_size(argv[2], strlen(argv[2]), &iteration_count),
            "failed to parse the iteration count");
        if (ERR_FAILED(error)) goto arg_parse_fail;
    }

    pthread_t *thread_ids = calloc(worker_count, sizeof(pthread_t));
    worker_param_t *params = calloc(worker_count, sizeof(worker_param_t));
    double *partial_sums = NULL;

    if (!can_return_double) {
        log_printf(LOG_WARN, "sizeof(double) is larger than sizeof(void *), using a fallback");

        partial_sums = calloc(worker_count, sizeof(double));

        for (size_t i = 0; i < worker_count; ++i) {
            partial_sums[i] = 0;
        }
    }

    for (size_t i = 0; i < worker_count; ++i) {
        params[i] = (worker_param_t) {
            .iteration_count = iteration_count,
            .worker_idx = i,
            .worker_count = worker_count,
            .output = partial_sums,
        };
    }

    log_printf(LOG_INFO, "Starting computation of π with %zu workers, %zu iterations...",
        worker_count, iteration_count);

    size_t started_thread_count = 0;

    for (; started_thread_count < worker_count; ++started_thread_count) {
        error = ERR((err_errno_t) pthread_create(
            &thread_ids[started_thread_count], NULL,
            worker_thread, &params[started_thread_count]
        ), "failed to start a worker thread");

        if (ERR_FAILED(error)) {
            break;
        }
    }

    double result = 0;

    for (size_t i = 0; i < started_thread_count; ++i) {
        void *returned_value = NULL;
        err_t join_error = ERR((err_errno_t) pthread_join(thread_ids[i], &returned_value),
            "failed to join a child thread");

        if (ERR_FAILED(join_error)) {
            err_log_free(LOG_WARN, &join_error);

            continue;
        }

        if (can_return_double) {
            double partial_sum = (result_t) { .opaque_result = returned_value }.partial_sum;
            result += partial_sum;
        } else {
            result += partial_sums[i];
        }
    }

    if (!ERR_FAILED(error)) {
        printf("π = %lf\n", 4 * result);
    }

    free(thread_ids);
    free(params);
    free(partial_sums);

arg_parse_fail:
    if (ERR_FAILED(error)) {
        err_log_free(LOG_ERR, &error);

        return 1;
    }

    pthread_exit(0);
}
