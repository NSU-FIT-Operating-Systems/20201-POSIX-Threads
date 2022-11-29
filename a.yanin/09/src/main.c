#include <assert.h>
#include <signal.h>
#include <stdatomic.h>
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
    ITERATIONS_BETWEEN_CHECKS = 1'000'000,
};

typedef struct {
    size_t iteration_count;
    size_t worker_idx;
    size_t worker_count;
    double partial_sum;
    size_t first_uncomputed_term_index;
} worker_param_t;

static volatile sig_atomic_t interrupted = false;

static void handle_sigint(int signum) {
    if (signum == SIGINT) {
        interrupted = true;
    }
}

static void finish_worker_thread(
    worker_param_t *worker_params,
    size_t first_uncomputed_term_index,
    double partial_sum
) {
    worker_params->partial_sum = partial_sum;
    worker_params->first_uncomputed_term_index = first_uncomputed_term_index;

    pthread_exit(0);
}

static void print_usage(void) {
    fputs("Usage: sigpi [<thread count>]\n", stderr);
}

static size_t initial_iteration(size_t worker_idx) {
    return worker_idx * TERMS_PER_UNIT;
}

static size_t next_iteration(size_t iteration, size_t worker_count) {
    size_t leap = iteration % 4 == 3 ? TERMS_PER_UNIT * (worker_count - 1) : 0;

    return iteration + 1 + leap;
}

static double compute_term(size_t iteration) {
    double sign = iteration % 2 == 0 ? 1 : -1;

    return sign / (iteration * 2 + 1);
}

static void *worker_thread(void *worker_params_opaque) {
    assert(worker_params_opaque != NULL);

    worker_param_t *worker_params = worker_params_opaque;
    size_t iteration_count = worker_params->iteration_count;
    size_t worker_count = worker_params->worker_count;
    size_t worker_idx = worker_params->worker_idx;
    double partial_sum = 0;

    size_t iteration;
    size_t next_check = ITERATIONS_BETWEEN_CHECKS;

    for (iteration = initial_iteration(worker_idx);
            iteration < iteration_count;
            iteration = next_iteration(iteration, worker_count)) {
        if (next_check-- == 0) {
            next_check = ITERATIONS_BETWEEN_CHECKS;

            if (interrupted) {
                // the current term (index `iteration`) hasn't been computed yet at this point
                break;
            }
        }

        partial_sum += compute_term(iteration);
    }

    finish_worker_thread(worker_params, iteration, partial_sum);

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

    sigset_t signal_set;
    sigemptyset(&signal_set);
    error = ERR((err_errno_t) pthread_sigmask(SIG_SETMASK, &signal_set, NULL),
        "failed to block signals in the main thread");
    if (ERR_FAILED(error)) goto sigmask_fail;

    pthread_t *thread_ids = calloc(worker_count, sizeof(pthread_t));
    worker_param_t *params = calloc(worker_count, sizeof(worker_param_t));

    if (ERR_FAILED(error = ERR((bool)(thread_ids != NULL),
                "could not allocate an array for thread handles")) ||
            ERR_FAILED(error = ERR((bool)(params != NULL),
                "ccould not allocate an array for worker thread parameters"))) {
        goto calloc_fail;
    }

    for (size_t i = 0; i < worker_count; ++i) {
        params[i] = (worker_param_t) {
            .iteration_count = iteration_count,
            .worker_idx = i,
            .worker_count = worker_count,
            .partial_sum = 0.,
            .first_uncomputed_term_index = 0,
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
            goto pthread_create_fail;
        }
    }

    double result = 0;

    sigaddset(&signal_set, SIGINT);
    sigaction(SIGINT, &(struct sigaction) { .sa_handler = handle_sigint }, NULL);
    err_t warn = ERR((err_errno_t) pthread_sigmask(SIG_SETMASK, &signal_set, NULL),
        "could not unblock SIGINT");

    if (ERR_FAILED(warn)) {
        err_log_printf_free(LOG_WARN, &warn,
            "This program will be unable to terminate by pressing ^C");
    } else {
        log_printf(LOG_INFO, "Press ^C to terminate computation early...");
    }

pthread_create_fail:
    for (size_t i = 0; i < started_thread_count; ++i) {
        err_t join_error = ERR((err_errno_t) pthread_join(thread_ids[i], NULL),
            "failed to join a child thread");

        if (ERR_FAILED(join_error)) {
            err_log_free(LOG_WARN, &join_error);

            continue;
        }

        result += params[i].partial_sum;
    }

    if (interrupted) {
        assert(started_thread_count == worker_count);

        size_t final_iteration = 0;

        for (size_t i = 0; i < worker_count; ++i) {
            if (params[i].first_uncomputed_term_index > final_iteration) {
                final_iteration = params[i].first_uncomputed_term_index;
            }
        }

        log_printf(LOG_INFO, "Finalizing computation after receiving a SIGINT (%zu iterations)...",
            final_iteration);

        for (size_t i = 0; i < worker_count; ++i) {
            for (size_t iteration = params[i].first_uncomputed_term_index;
                    iteration < final_iteration;
                    iteration = next_iteration(iteration, worker_count)) {
                result += compute_term(iteration);
            }
        }
    }

    if (!ERR_FAILED(error)) {
        printf(
            "computed π = %.18lf\n"
            "    real π = %.18lf\n",
            4 * result,
            3.141592653589793238462643383279
        );
    }

calloc_fail:
    free(thread_ids);
    free(params);

sigmask_fail:
arg_parse_fail:
    if (ERR_FAILED(error)) {
        err_log_free(LOG_ERR, &error);

        return 1;
    }

    pthread_exit(0);
}
