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
    PHILOSOPHER_COUNT = 5,
    DELAY_NS = 30'000'000,
    INITIAL_FOOD_UNITS = 50,
};

typedef struct {
    pthread_mutex_t mtx;
    int units;
} food_store_t;

typedef struct {
    size_t id;
    time_t sleep_seconds;
    pthread_mutex_t *forks;
    food_store_t *food_store;
} worker_params_t;

static void print_usage(void) {
    fputs(
        "Usage: phil [<sleep time>]\n"
        "\t<sleep time>\n"
        "\t\thow much to delay the first philosopher initially for, in seconds\n",
        stderr
    );
}

static err_t unlock_mtx(pthread_mutex_t *mtx) {
    return ERR((err_errno_t) pthread_mutex_unlock(mtx),
        "pthread_mutex_unlock failed");
}

static err_t take_fork(size_t id, pthread_mutex_t *mtx, size_t fork_id, char const *hand) {
    err_t err = ERR((err_errno_t) pthread_mutex_lock(mtx),
        "pthread_mutex_lock failed");

    if (!ERR_FAILED(err)) {
        log_printf(LOG_INFO, "The philosopher #%zu has taken the %s fork (#%zu)",
            id + 1, hand, fork_id + 1);
    }

    return err;
}

static int food_store_take(food_store_t *food_store) {
    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_lock(&food_store->mtx),
        "could not lock the food store"));

    if (food_store->units >= 0) {
        --food_store->units;
    }

    int dish = food_store->units;
    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_unlock(&food_store->mtx),
        "could not unlock the food store"));

    return dish;
}

static void *worker_thread(void *worker_params_opaque) {
    assert(worker_params_opaque != NULL);

    worker_params_t *worker_params = worker_params_opaque;
    size_t id = worker_params->id;
    pthread_mutex_t *forks = worker_params->forks;
    food_store_t *food_store = worker_params->food_store;

    size_t left_fork_id = id;
    size_t right_fork_id = id + 1;

    if (right_fork_id == PHILOSOPHER_COUNT) {
        // this courageous man will go against the crowd
        right_fork_id = id;
        left_fork_id = 0;
    }

    int dish = -1;

    pthread_mutex_t *left_fork = &forks[left_fork_id];
    pthread_mutex_t *right_fork = &forks[right_fork_id];

    while ((dish = food_store_take(food_store)) != -1) {
        if (id == 0) {
            err_t sleep_error = ERR(wrapper_nanosleep(&(struct timespec) {
                .tv_sec = worker_params->sleep_seconds,
            }), NULL);

            if (ERR_FAILED(sleep_error)) {
                err_log_printf_free(LOG_WARN, &sleep_error,
                    "Could not put the first philosopher to sleep");
            }
        }

        log_printf(LOG_INFO, "The philosopher #%zu has retrieved dish %d", id + 1, dish + 1);
        ERR_ASSERT(ERR(take_fork(id + 1, left_fork, left_fork_id + 1, "left"),
            "could not pick up the left fork"));
        ERR_ASSERT(ERR(take_fork(id + 1, right_fork, right_fork_id + 1, "right"),
            "could not pick up the right fork"));

        log_printf(LOG_INFO, "The philosopher #%zu has started eating dish %d", id + 1, dish + 1);

        err_t sleep_error = ERR(wrapper_nanosleep(&(struct timespec) {
            .tv_sec = DELAY_NS / 1'000'000'000,
            .tv_nsec = DELAY_NS % 1'000'000'000,
        }), NULL);

        if (ERR_FAILED(sleep_error)) {
            err_log_printf_free(LOG_WARN, &sleep_error,
                "Could not make the philosopher #%zu start eating", id);
        }

        log_printf(LOG_INFO, "The philosopher #%zu is done eating dish %d", id + 1, dish + 1);
        ERR_ASSERT(ERR(unlock_mtx(left_fork), "could not put down the left fork"));
        ERR_ASSERT(ERR(unlock_mtx(right_fork), "could not put down the right fork"));
    }

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

static err_t parse_time_t(char const *buf, size_t len, time_t *result) {
    assert(buf != NULL);
    assert(result != NULL);
    assert(buf[len] == '\0');

    unsigned long long ull_result = 0;
    err_t error = ERR(parse_unsigned_integer(buf, len, &ull_result), "failed to parse an integer");
    if (ERR_FAILED(error)) return error;

    time_t converted = ull_result;

    bool valid = converted >= 0 && ((uintmax_t) converted == (uintmax_t) ull_result);

    error = ERR(valid, "the integer was too large");
    if (ERR_FAILED(error)) return error;

    *result = ull_result;

    return error;
}

int main(int argc, char *argv[]) {
    if (!(argc >= 1 && argc <= 2)) {
        print_usage();

        return 1;
    }

    err_t error = OK;
    time_t sleep_seconds = 0;

    if (argc >= 2) {
        error = ERR(parse_time_t(argv[1], strlen(argv[1]), &sleep_seconds),
            "failed to parse the sleep time");
        if (ERR_FAILED(error)) goto arg_parse_fail;
    }

    food_store_t food_store = {
        .units = INITIAL_FOOD_UNITS,
    };

    error = ERR((err_errno_t) pthread_mutex_init(&food_store.mtx, NULL),
        "could not initialize the food store mutex");
    if (ERR_FAILED(error)) goto food_mtx_init_fail;

    pthread_t thread_ids[PHILOSOPHER_COUNT];
    pthread_mutex_t forks[PHILOSOPHER_COUNT];
    worker_params_t params[PHILOSOPHER_COUNT];

    for (size_t i = 0; i < PHILOSOPHER_COUNT; ++i) {
        params[i] = (worker_params_t) {
            .id = i,
            .sleep_seconds = sleep_seconds,
            .forks = forks,
            .food_store = &food_store,
        };
    }

    size_t initialized_fork_count = 0;

    for (; initialized_fork_count < PHILOSOPHER_COUNT; ++initialized_fork_count) {
        error = ERR((err_errno_t) pthread_mutex_init(&forks[initialized_fork_count], NULL),
            "could not initialize a fork mutex");
        if (ERR_FAILED(error)) goto fork_mtx_init_fail;
    }

    log_printf(LOG_INFO, "Creating %d philosophers with %d food units in the store...",
        PHILOSOPHER_COUNT, food_store.units);

    size_t started_thread_count = 0;

    for (; started_thread_count < PHILOSOPHER_COUNT; ++started_thread_count) {
        error = ERR((err_errno_t) pthread_create(
            &thread_ids[started_thread_count], NULL,
            worker_thread, &params[started_thread_count]
        ), "failed to create a philosopher");
        if (ERR_FAILED(error)) goto pthread_create_fail;
    }

pthread_create_fail:
    for (size_t i = 0; i < started_thread_count; ++i) {
        err_t join_error = ERR((err_errno_t) pthread_join(thread_ids[i], NULL),
            "failed to join a philosopher thread");

        if (ERR_FAILED(join_error)) {
            err_log_free(LOG_WARN, &join_error);

            continue;
        }
    }

fork_mtx_init_fail:
    for (size_t i = 0; i < initialized_fork_count; ++i) {
        err_t destroy_error = ERR((err_errno_t) pthread_mutex_destroy(&forks[i]),
            "pthread_mutex_destroy failed");

        if (ERR_FAILED(destroy_error)) {
            err_log_printf_free(LOG_WARN, &destroy_error, "Failed to destroy a fork mutex #%zu",
                i);
        }
    }

food_mtx_init_fail:
    {
        err_t destroy_error = ERR((err_errno_t) pthread_mutex_destroy(&food_store.mtx),
            "could not destroy the food store mutex");

        if (ERR_FAILED(destroy_error)) {
            err_log_free(LOG_WARN, &destroy_error);
        }
    }

arg_parse_fail:
    if (ERR_FAILED(error)) {
        err_log_free(LOG_ERR, &error);

        return 1;
    }

    pthread_exit(0);
}
