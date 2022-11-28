#include <assert.h>
#include <string.h>
#include <time.h>

#include <pthread.h>
#include <signal.h>

#include <common/io.h>

#define LOG_LEVEL LOG_INFO
#define LOG_MODE LOG_MODE_SYNC
#include <common/log/log.h>

#include <common/posix/time.h>

#define VEC_LABEL thr
#define VEC_ELEMENT_TYPE pthread_t
#define VEC_CONFIG (COLLECTION_DECLARE | COLLECTION_DEFINE | COLLECTION_STATIC)
#include <common/collections/vec.h>

#include "error.h"
#include "quasi.h"

enum {
    SIZE_CAP = 10,
    PRODUCERS = 5,
    CONSUMERS = 5,
};

typedef struct {
    quasi_t *queue;
    size_t id;
    pthread_t main_thread;
} worker_params_t;

atomic_bool interrupted = false;

static void print_usage(void) {
    fputs("Usage: quasi\n", stderr);
}

static void *producer_thread(void *worker_params_opaque) {
    worker_params_t *worker_params = worker_params_opaque;
    quasi_t *queue = worker_params->queue;
    size_t id = worker_params->id;

    err_t error = OK;

    for (size_t line = 0; !interrupted; ++line) {
        string_t str;
        error = ERR(string_sprintf(&str, "producer %zu, line %zu", id, line),
            "could not allocate a string");
        if (ERR_FAILED(error)) goto string_sprintf_fail;

        if (quasi_push(queue, str) == QUASI_STATE_DROPPED) {
            string_free(&str);
        } else if (line % (id * 1000) == 0) {
            quasi_drop(queue);
            log_printf(LOG_WARN, "Producer %zu dropped the queue", id);
        }

        log_printf(LOG_DEBUG, "Producer %zu pushed line %zu", id, line);
    }

    free(worker_params_opaque);

    return 0;

string_sprintf_fail:
    pthread_kill(worker_params->main_thread, SIGINT);
    err_log_printf_free(LOG_ERR, &error, "Producer %zu has failed", id);

    free(worker_params_opaque);

    return (void *) 1;
}

static void *consumer_thread(void *worker_params_opaque) {
    worker_params_t *worker_params = worker_params_opaque;
    quasi_t *queue = worker_params->queue;
    size_t id = worker_params->id;

    while (!interrupted) {
        string_t str;

        if (quasi_pop(queue, &str) != QUASI_STATE_OK) {
            continue;
        }

        log_printf(LOG_INFO, "Consumer %zu got a message: %s",
            id, string_as_cptr(&str));
        string_free(&str);
    }

    free(worker_params_opaque);

    return 0;
}

int main(int argc, char *[]) {
    if (argc != 1) {
        print_usage();

        return 1;
    }

    err_t error = OK;

    sigset_t signal_set;
    sigemptyset(&signal_set);
    error = ERR((err_errno_t) pthread_sigmask(SIG_SETMASK, &signal_set, NULL),
        "failed to block signals in the main thread");
    if (ERR_FAILED(error)) goto pthread_sigmask_fail;

    sigaddset(&signal_set, SIGINT);

    quasi_t queue;
    error = ERR(quasi_new(SIZE_CAP, &queue), "could not create a message queue");
    if (ERR_FAILED(error)) goto quasi_new_fail;

    vec_thr_t threads = vec_thr_new();
    error = ERR((err_errno_t) vec_thr_resize(&threads, PRODUCERS + CONSUMERS),
        "could not allocate a vector for thread ids");
    if (ERR_FAILED(error)) goto vec_resize_fail;

    for (size_t i = 0; i < PRODUCERS + CONSUMERS; ++i) {
        worker_params_t *worker_params = malloc(sizeof(worker_params_t));
        error = ERR((bool)(worker_params != NULL), "could not allocate memory for worker params");
        if (ERR_FAILED(error)) goto worker_params_malloc_fail;

        *worker_params = (worker_params_t) {
            .queue = &queue,
            .id = 1 + (i < PRODUCERS ? i : i - PRODUCERS),
            .main_thread = pthread_self(),
        };

        pthread_t thread_id;
        void *(*thread_func)(void *) = i < PRODUCERS ? producer_thread : consumer_thread;
        error = ERR((err_errno_t) pthread_create(&thread_id, NULL, thread_func, worker_params),
            "could not create a thread");
        if (ERR_FAILED(error)) goto pthread_create_fail;

        vec_thr_push(&threads, thread_id);
    }

    log_printf(LOG_INFO, "Started %d producers and %d consumers", PRODUCERS, CONSUMERS);

    {
        int sig;
        sigwait(&signal_set, &sig);
    }

worker_params_malloc_fail:
pthread_create_fail:
    quasi_drop(&queue);
    interrupted = true;

    for (size_t i = 0; i < vec_thr_len(&threads); ++i) {
        err_t join_error = ERR((err_errno_t) pthread_join(*vec_thr_get(&threads, i), NULL),
            "failed to join a child thread");

        if (ERR_FAILED(join_error)) {
            err_log_free(LOG_WARN, &join_error);
        }
    }

    vec_thr_free(&threads);

vec_resize_fail:
    quasi_free(&queue);

quasi_new_fail:
pthread_sigmask_fail:
    if (ERR_FAILED(error)) {
        err_log_free(LOG_ERR, &error);
    }

    pthread_exit(0);
}
