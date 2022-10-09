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
    THREAD_NUM = 2,
    LINE_COUNT = 10,
};

typedef struct {
    size_t id;
    pthread_barrier_t *start_barrier;
    pthread_mutex_t *mtxes;
} worker_params_t;

static void print_usage(void) {
    fputs("Usage: interprint\n", stderr);
}

static size_t next_mtx_id(size_t id) {
    return (id + 1) % (THREAD_NUM + 1);
}

static void print_loop(worker_params_t *worker_params) {
    size_t id = worker_params->id;
    pthread_mutex_t *mtxes = worker_params->mtxes;
    pthread_barrier_t *start_barrier = worker_params->start_barrier;

    if (id == 0) {
        log_printf(LOG_DEBUG, "Thread %zu locks mtx %zu", id, id + 1);
        ERR_ASSERT(ERR((err_errno_t) pthread_mutex_lock(&mtxes[1]),
            "could not lock the first mutex"));
    }

    size_t mtx_id = id;

    if (id > 0) {
        ++mtx_id;
    }

    log_printf(LOG_DEBUG, "Thread %zu locks mtx %zu", id, mtx_id);
    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_lock(&mtxes[mtx_id]),
        "could not lock a mutex"));

    int barrier_retval = pthread_barrier_wait(start_barrier);

    if (barrier_retval != PTHREAD_BARRIER_SERIAL_THREAD) {
        ERR_ASSERT(ERR((err_errno_t) barrier_retval,
            "could not perform the initial synchronization at the barrier"));
    }

    size_t i = 0;
    bool initialized = id != 0;

    while (i < LINE_COUNT) {
        if (initialized) {
            log_printf(LOG_DEBUG, "Thread %zu locks mtx %zu", id, next_mtx_id(mtx_id));
            ERR_ASSERT(ERR((err_errno_t) pthread_mutex_lock(&mtxes[next_mtx_id(mtx_id)]),
                "could not lock a mutex"));
        } else {
            initialized = true;
        }

        if (next_mtx_id(mtx_id) == 1) {
            log_printf(LOG_INFO, "Thread %zu: line %zu", id, i);
        } else {
            ++i;
            log_printf(LOG_DEBUG, "Thread %zu increments line counter to %zu", id, i);
        }

        log_printf(LOG_DEBUG, "Thread %zu unlocks mtx %zu", id, mtx_id);
        ERR_ASSERT(ERR((err_errno_t) pthread_mutex_unlock(&mtxes[mtx_id]),
            "could not unlock a mutex"));

        mtx_id = next_mtx_id(mtx_id);
    }

    log_printf(LOG_DEBUG, "Thread %zu unlocks mtx %zu", id, mtx_id);
    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_unlock(&mtxes[mtx_id]),
        "could not unlock the remaining mutex"));
}

static void *worker_thread(void *worker_params_opaque) {
    assert(worker_params_opaque != NULL);

    print_loop(worker_params_opaque);

    return 0;
}

int main(int argc, char *[]) {
    if (argc != 1) {
        print_usage();

        return 1;
    }

    err_t error = OK;

    pthread_barrier_t start_barrier;
    pthread_mutex_t mtxes[THREAD_NUM + 1];
    pthread_t thread_ids[THREAD_NUM - 1];
    worker_params_t params[THREAD_NUM];

    for (size_t i = 0; i < THREAD_NUM; ++i) {
        params[i] = (worker_params_t) {
            .id = i,
            .start_barrier = &start_barrier,
            .mtxes = mtxes,
        };
    }

    error = ERR((err_errno_t) pthread_barrier_init(&start_barrier, NULL, THREAD_NUM),
        "could not initialize the start barrier");
    if (ERR_FAILED(error)) goto barrier_init_fail;

    pthread_mutexattr_t mtx_attr;

    error = ERR((err_errno_t) pthread_mutexattr_init(&mtx_attr),
        "could not initialize mutex attributes");
    if (ERR_FAILED(error)) goto mtx_attr_init_fail;

    pthread_mutexattr_settype(&mtx_attr, PTHREAD_MUTEX_ERRORCHECK);

    size_t initialized_mtx_count = 0;

    for (; initialized_mtx_count < THREAD_NUM + 1; ++initialized_mtx_count) {
        error = ERR((err_errno_t) pthread_mutex_init(&mtxes[initialized_mtx_count], &mtx_attr),
            "could not initialize a mutex");
        if (ERR_FAILED(error)) goto mtx_init_fail;
    }

    size_t started_thread_count = 0;

    // the main thread is already there, so we skip it
    for (; started_thread_count < THREAD_NUM - 1; ++started_thread_count) {
        error = ERR((err_errno_t) pthread_create(
            &thread_ids[started_thread_count], NULL,
            worker_thread, &params[started_thread_count + 1]
        ), "failed to create a philosopher");
        if (ERR_FAILED(error)) goto pthread_create_fail;
    }

    print_loop(&params[0]);

pthread_create_fail:
    for (size_t i = 0; i < started_thread_count; ++i) {
        err_t join_error = ERR((err_errno_t) pthread_join(thread_ids[i], NULL),
            "failed to join a child thread");

        if (ERR_FAILED(join_error)) {
            err_log_free(LOG_WARN, &join_error);

            continue;
        }
    }

mtx_init_fail:
    for (size_t i = 0; i < initialized_mtx_count; ++i) {
        err_t destroy_error = ERR((err_errno_t) pthread_mutex_destroy(&mtxes[i]),
            "failed to destroy a mutex");

        if (ERR_FAILED(destroy_error)) {
            err_log_free(LOG_WARN, &destroy_error);
        }
    }

    pthread_mutexattr_destroy(&mtx_attr);

mtx_attr_init_fail:
    {
        err_t destroy_error = ERR((err_errno_t) pthread_barrier_destroy(&start_barrier),
            "could not destroy the start barrier");

        if (ERR_FAILED(destroy_error)) {
            err_log_free(LOG_WARN, &destroy_error);
        }
    }

barrier_init_fail:
    if (ERR_FAILED(error)) {
        err_log_free(LOG_ERR, &error);

        return 1;
    }

    pthread_exit(0);
}
