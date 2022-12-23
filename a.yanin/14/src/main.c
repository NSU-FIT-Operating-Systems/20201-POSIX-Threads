#include <assert.h>
#include <semaphore.h>
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
    sem_t *self_sem;
    sem_t *other_sem;
} worker_params_t;

static void print_usage(void) {
    fputs("Usage: intersem\n", stderr);
}

static int uninterruptible_wait(sem_t *sem) {
    int ret = -1;

    do {
        errno = 0;
        ret = sem_wait(sem);
    } while (ret == -1 && errno == EINTR);

    return ret;
}

static void print_loop(worker_params_t *worker_params) {
    size_t id = worker_params->id;
    sem_t *self_sem = worker_params->self_sem;
    sem_t *other_sem = worker_params->other_sem;

    for (size_t i = 0; i < LINE_COUNT; ++i) {
        ERR_ASSERT(ERR((err_errno_t) uninterruptible_wait(self_sem),
            "could not decrement a semaphore"));
        log_printf(LOG_INFO, "Thread %zu: line %zu", id, i);
        ERR_ASSERT(ERR((err_errno_t) sem_post(other_sem), "could not increment a semaphore"));
    }
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

    sem_t sems[THREAD_NUM];
    pthread_t thread_ids[THREAD_NUM - 1];
    worker_params_t params[THREAD_NUM];

    for (size_t i = 0; i < THREAD_NUM; ++i) {
        ERR_ASSERT(ERR((err_errno_t) sem_init(&sems[i], 0, 0), "could not initialize a semaphore"));
    }

    ERR_ASSERT(ERR((err_errno_t) sem_post(&sems[0]), "could not increment the first semaphore"));

    for (size_t i = 0; i < THREAD_NUM; ++i) {
        size_t self_id = i;
        size_t other_id = (i + 1) % THREAD_NUM;

        params[i] = (worker_params_t) {
            .id = i,
            .self_sem = &sems[self_id],
            .other_sem = &sems[other_id],
        };
    }

    size_t started_thread_count = 0;

    // the main thread is already there, so we skip it
    for (; started_thread_count < THREAD_NUM - 1; ++started_thread_count) {
        error = ERR((err_errno_t) pthread_create(
            &thread_ids[started_thread_count], NULL,
            worker_thread, &params[started_thread_count + 1]
        ), "failed to create a child thread");
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

    for (size_t i = 0; i < THREAD_NUM; ++i) {
        ERR_ASSERT(ERR((err_errno_t) sem_destroy(&sems[i]), "could not destroy a semaphore"));
    }

    if (ERR_FAILED(error)) {
        err_log_free(LOG_ERR, &error);

        return 1;
    }

    pthread_exit(0);
}
