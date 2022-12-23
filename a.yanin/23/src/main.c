#include <assert.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

#include <common/io.h>
#include <common/log/log.h>
#include <common/posix/time.h>

#include "error.h"

typedef string_t *strptr_t;

#define DLIST_ELEMENT_TYPE strptr_t
#define DLIST_LABEL strptr
#define DLIST_CONFIG (COLLECTION_DECLARE | COLLECTION_DEFINE | COLLECTION_STATIC)
#include <common/collections/dlist.h>

static const long long sleep_per_char_us = 50'000;

typedef struct {
    bool start;
    size_t thread_count;
    size_t finished_count;
    pthread_mutex_t mtx;
    pthread_cond_t cond;
} worker_start_t;

typedef struct {
    string_t *line;
    worker_start_t *start;
    pthread_mutex_t *list_mtx;
    dlist_strptr_t *entries;
} worker_params_t;

static void print_usage(void) {
    fputs("Usage: sleeplist\n", stderr);
}

static void finalize_sleepsort(worker_params_t *worker_params) {
    // precondition: only the current thread has access to the shared resources
    worker_start_t *start = worker_params->start;
    pthread_mutex_t *list_mtx = worker_params->list_mtx;
    dlist_strptr_t *entries = worker_params->entries;

    for (dlist_strptr_node_t const *node = dlist_strptr_head(entries);
            node != NULL;
            node = dlist_strptr_next(node)) {
        string_t *const *str = dlist_strptr_get(node);
        void const *buf = string_as_cptr(*str);
        size_t len = string_len(*str);
        fwrite(buf, len, 1, stdout);
        fputc('\n', stdout);
    }

    // free the resources
    for (dlist_strptr_node_t *node = dlist_strptr_head_mut(entries);
            node != NULL;
            node = dlist_strptr_next_mut(node)) {
        string_t **str = dlist_strptr_get_mut(node);
        string_free(*str);
        free(*str);
    }

    dlist_strptr_free(entries);
    pthread_mutex_destroy(list_mtx);
    pthread_cond_destroy(&start->cond);
    pthread_mutex_destroy(&start->mtx);
    free(entries);
    free(list_mtx);
    free(start);
}

static void *child_thread(void *worker_params_opaque) {
    worker_params_t *worker_params = worker_params_opaque;

    string_t *line = worker_params->line;
    worker_start_t *start = worker_params->start;
    pthread_mutex_t *list_mtx = worker_params->list_mtx;
    dlist_strptr_t *entries = worker_params->entries;

    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_lock(&start->mtx),
        "could not lock start->mtx"));

    while (!start->start) {
        ERR_ASSERT(ERR((err_errno_t) pthread_cond_wait(&start->cond, &start->mtx),
            "could not wait on start->cond"));
    }

    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_unlock(&start->mtx),
        "could not unlock start->mtx"));

    size_t len = string_len(line);
    long long sleep_time_us = sleep_per_char_us * (long long) len;
    struct timespec sleep_time = {
        .tv_sec = sleep_time_us / 1'000'000,
        .tv_nsec = (sleep_time_us % 1'000'000) * 1'000,
    };

    {
        err_t sleep_error = ERR(wrapper_nanosleep(&sleep_time), "failed to put a thread to sleep");

        if (ERR_FAILED(sleep_error)) {
            err_log_free(LOG_WARN, &sleep_error);
        }
    }

    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_lock(list_mtx),
        "could not lock the list mutex"));

    {
        err_t error = ERR(dlist_strptr_append(entries, line, NULL),
            "failed to store an entered line");

        if (ERR_FAILED(error)) {
            err_log_free(LOG_ERR, &error);
        } else {
            log_printf(LOG_DEBUG, "added a %zu-sized string to the list", len);
        }
    }

    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_lock(&start->mtx),
        "could not lock start->mtx"));
    ++start->finished_count;
    bool finished = start->finished_count == start->thread_count;
    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_unlock(&start->mtx),
        "could not unlock start->mtx"));

    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_unlock(list_mtx),
        "could not unlock the list mutex"));

    if (finished) {
        finalize_sleepsort(worker_params);
    }

    free(worker_params);

    return 0;
}

static err_t dispatch_line(
    pthread_attr_t *thread_attr,
    pthread_mutex_t *list_mtx,
    dlist_strptr_t *entries,
    worker_start_t *start,
    bool *eof
) {
    err_t error = OK;

    string_t *line = malloc(sizeof(string_t));
    error = ERR((bool)(line != NULL), "failed to allocate a string for an input line");
    if (ERR_FAILED(error)) goto malloc_string_fail;

    error = ERR(string_new(line), "failed to allocate a string for an input line");
    if (ERR_FAILED(error)) goto string_new_fail;

    common_error_code_t error_code = COMMON_ERROR_CODE_OK;
    error = ERR((error_code = read_line(stdin, line)), "failed to read a line from stdin");

    if (ERR_FAILED(error)) {
        if (error_code == COMMON_ERROR_CODE_NOT_FOUND) {
            err_free(&error);
            error = OK;
            *eof = true;
        }

        goto read_line_fail;
    }

    worker_params_t *worker_params = malloc(sizeof(worker_params_t));
    error = ERR((bool)(worker_params != NULL), "failed to allocate memory for thread data");
    if (ERR_FAILED(error)) goto malloc_worker_params_fail;

    *worker_params = (worker_params_t) {
        .line = line,
        .start = start,
        .list_mtx = list_mtx,
        .entries = entries,
    };

    pthread_t thread_id;
    error = ERR((err_errno_t) pthread_create(
        &thread_id,
        thread_attr,
        child_thread,
        worker_params
    ), "failed to create a child thread");
    if (ERR_FAILED(error)) goto pthread_create_fail;

    // synchronization is unnecessary here since all the threads are waiting for
    // start->start to get set, which it isn't
    ++start->thread_count;

    return error;

pthread_create_fail:
    free(worker_params);

malloc_worker_params_fail:
read_line_fail:
    string_free(line);

string_new_fail:
    free(line);

malloc_string_fail:
    return error;
}

int main(int argc, char *[]) {
    if (argc != 1) {
        print_usage();

        return 1;
    }

    err_t error = OK;

    pthread_mutexattr_t mtx_attr;
    ERR_ASSERT(ERR((err_errno_t) pthread_mutexattr_init(&mtx_attr),
        "could not initialize mutex attributes"));
    pthread_mutexattr_settype(&mtx_attr, PTHREAD_MUTEX_ERRORCHECK);

    pthread_mutex_t *list_mtx = malloc(sizeof(pthread_mutex_t));
    error = ERR((bool)(list_mtx != NULL), "failed to allocate memory for the list mutex");
    if (ERR_FAILED(error)) goto malloc_list_mtx_fail;
    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_init(list_mtx, &mtx_attr),
        "could not initialize a mutex"));

    dlist_strptr_t *entries = malloc(sizeof(dlist_strptr_t));
    error = ERR((bool)(entries != NULL), "failed to allocate memory for the list");
    if (ERR_FAILED(error)) goto malloc_entries_fail;
    *entries = dlist_strptr_new();

    // initialize the shared worker_start_t struct
    worker_start_t *start = malloc(sizeof(worker_start_t));
    error = ERR((bool)(start != NULL), "failed to allocate memory for the shared worker params");
    if (ERR_FAILED(error)) goto malloc_start_fail;
    *start = (worker_start_t) {
        .start = false,
    };

    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_init(&start->mtx, &mtx_attr),
        "could not initialize start->mutex"));
    pthread_mutexattr_destroy(&mtx_attr);

    ERR_ASSERT(ERR((err_errno_t) pthread_cond_init(&start->cond, NULL),
        "could not initialize start->cond"));

    pthread_attr_t thread_attr;
    ERR_ASSERT(ERR((err_errno_t) pthread_attr_init(&thread_attr),
        "could not initialize thread attributes"));
    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);

    bool eof = false;

    do {
        error = ERR(dispatch_line(&thread_attr, list_mtx, entries, start, &eof),
            "failed to dispatch a line from stdin");
    } while (!eof && !ERR_FAILED(error));

    pthread_attr_destroy(&thread_attr);

    if (start->thread_count == 0) {
        worker_params_t params = {
            .line = NULL,
            .start = start,
            .list_mtx = list_mtx,
            .entries = entries,
        };

        finalize_sleepsort(&params);
    }

    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_lock(&start->mtx),
        "could not lock start->mtx"));
    start->start = true;
    ERR_ASSERT(ERR((err_errno_t) pthread_cond_broadcast(&start->cond),
        "could not notify the child threads"));
    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_unlock(&start->mtx),
        "could not unlock start->mtx"));

    // set the pointers to NULL to avoid double-free
    list_mtx = NULL;
    entries = NULL;
    start = NULL;

malloc_start_fail:
    free(entries);

malloc_entries_fail:
    free(list_mtx);

malloc_list_mtx_fail:
    if (ERR_FAILED(error)) {
        err_log_free(LOG_ERR, &error);
    }

    pthread_exit(0);
}
