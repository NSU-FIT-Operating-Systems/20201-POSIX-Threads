#include <assert.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

#include <common/collections/string.h>
#include <common/io.h>
#include <common/log/log.h>
#include <common/posix/time.h>

#include "error.h"

#define DLIST_ELEMENT_TYPE string_t
#define DLIST_LABEL str
#include <common/collections/dlist.h>

typedef struct {
    dlist_str_t *list;
    pthread_mutex_t *mtx;
    pthread_cond_t *interrupted_cond;
} worker_params_t;

static volatile sig_atomic_t interrupted = false;

static void handle_sigint(int signum) {
    if (signum == SIGINT) {
        interrupted = true;
    }
}

static void print_usage(void) {
    fputs("Usage: syncbubble\n", stderr);
}

static void bubble_sort(dlist_str_t *list) {
    assert(list != NULL);

    dlist_str_node_t const *sorted_start = NULL;
    bool swapped = false;

    do {
        swapped = false;

        for (dlist_str_node_t *node = dlist_str_next_mut(dlist_str_head_mut(list));
                node != NULL && node != sorted_start;
                node = dlist_str_next_mut(node)) {
            dlist_str_node_t *prev = dlist_str_prev_mut(node);
            string_t const *current_str = dlist_str_get(node);
            string_t const *prev_str = dlist_str_get(prev);

            if (string_cmp(prev_str, current_str) > 0) {
                dlist_str_swap(list, prev, node);
                swapped = true;
            }
        }

        if (sorted_start == NULL) {
            sorted_start = dlist_str_end(list);
        } else {
            sorted_start = dlist_str_prev(sorted_start);
        }
    } while (swapped);
}

static void *child_thread(void *worker_params_opaque) {
    assert(worker_params_opaque != NULL);

    worker_params_t *worker_params = worker_params_opaque;
    dlist_str_t *list = worker_params->list;
    pthread_mutex_t *mtx = worker_params->mtx;
    pthread_cond_t *interrupted_cond = worker_params->interrupted_cond;

    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_lock(mtx), "could not lock the list mutex"));

    struct timespec deadline = {0};
    ERR_ASSERT(ERR(wrapper_clock_gettime(CLOCK_MONOTONIC, &deadline), NULL));
    deadline.tv_sec += 5;

    while (!interrupted) {
        int res = -1;
        err_t error = ERR((err_errno_t)(
            res = pthread_cond_timedwait(interrupted_cond, mtx, &deadline)
        ), "could not wait on a condition variable");

        if (ERR_FAILED(error) && res == ETIMEDOUT) {
            log_printf(LOG_DEBUG, "Sorting the list...");
            bubble_sort(list);
            deadline.tv_sec += 5;
        } else {
            ERR_ASSERT(error);
        }
    }

    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_unlock(mtx), "could not unlock the list mutex"));

    return 0;
}

static void print_list(dlist_str_t *list) {
    size_t i = 0;

    puts("List entries:");

    for (dlist_str_node_t const *node = dlist_str_head(list);
            node != NULL;
            node = dlist_str_next(node), ++i) {
        string_t const *str = dlist_str_get(node);
        printf("- #%zu: ", i);
        fwrite(string_as_ptr(str), string_len(str), 1, stdout);
        puts("");
    }
}

static err_t process_line(dlist_str_t *list, pthread_mutex_t *mtx) {
    err_t error = OK;

    string_t line;
    error = ERR(string_new(&line), "failed to allocate a string for an input line");
    if (ERR_FAILED(error)) goto string_new_fail;
    bool line_owned = true;

    common_error_code_t error_code = COMMON_ERROR_CODE_OK;
    errno = 0;
    error = ERR((error_code = read_line(stdin, &line)), "failed to read a line from stdin");

    if (ERR_FAILED(error)) {
        if (error_code == COMMON_ERROR_CODE_NOT_FOUND || errno == EINTR) {
            err_free(&error);
            error = OK;
            interrupted = true;
        }

        goto read_line_fail;
    }

    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_lock(mtx), "could not lock the list mutex"));

    if (string_len(&line) == 0) {
        print_list(list);
    } else {
        error = ERR(dlist_str_append(list, line, NULL), "failed to store the enterred line");
        if (ERR_FAILED(error)) goto dlist_str_append_fail;
        line_owned = false;
    }

dlist_str_append_fail:
    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_unlock(mtx), "could not unlock the list mutex"));

read_line_fail:
    if (line_owned) {
        string_free(&line);
    }

string_new_fail:
    return error;
}

static err_t read_lines(dlist_str_t *list, pthread_mutex_t *mtx) {
    err_t error = OK;

    while (!interrupted) {
        error = ERR(process_line(list, mtx), "could not read a line from stdin");
        if (ERR_FAILED(error)) goto process_line_fail;
    }

process_line_fail:
    return error;
}

int main(int argc, char *[]) {
    if (argc != 1) {
        print_usage();

        return 1;
    }

    err_t error = OK;

    dlist_str_t list = dlist_str_new();

    pthread_mutexattr_t mtx_attr;
    ERR_ASSERT(ERR((err_errno_t) pthread_mutexattr_init(&mtx_attr),
        "could not initialize mutex attributes"));
    pthread_mutexattr_settype(&mtx_attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_t mtx;
    ERR_ASSERT(ERR((err_errno_t) pthread_mutex_init(&mtx, &mtx_attr),
        "could not initialize a mutex"));
    pthread_condattr_t interrupted_cond_attr;
    ERR_ASSERT(ERR((err_errno_t) pthread_condattr_init(&interrupted_cond_attr),
        "could not initialize condition variable attributes"));
    ERR_ASSERT(ERR((err_errno_t) pthread_condattr_setclock(&interrupted_cond_attr, CLOCK_MONOTONIC),
        "could not set the clock to be used by the condition variable"));
    pthread_cond_t interrupted_cond;
    ERR_ASSERT(ERR((err_errno_t) pthread_cond_init(&interrupted_cond, &interrupted_cond_attr),
        "could not initialize a condition variable"));

    worker_params_t worker_params = {
        .list = &list,
        .mtx = &mtx,
        .interrupted_cond = &interrupted_cond,
    };

    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    ERR_ASSERT(ERR((err_errno_t) pthread_sigmask(SIG_BLOCK, &signal_set, NULL),
        "failed to block signals in the main thread"));

    pthread_t child_thread_id;
    error = ERR((err_errno_t) pthread_create(&child_thread_id, NULL, child_thread, &worker_params),
        "failed to create a child thread");
    if (ERR_FAILED(error)) goto pthread_create_fail;

    sigaction(SIGINT, &(struct sigaction) { .sa_handler = handle_sigint }, NULL);
    {
        err_t unblock_err = ERR((err_errno_t) pthread_sigmask(SIG_UNBLOCK, &signal_set, NULL),
            "failed to unblock SIGINT");

        if (ERR_FAILED(unblock_err)) {
            err_log_printf_free(LOG_WARN, &unblock_err,
                "The program will be unable to terminate by ^C");
        } else {
            log_printf(LOG_INFO, "Press ^C to terminate the program...");
        }
    }

    error = ERR(read_lines(&list, &mtx), "the main thread has encountered an error");
    // carry along, no need to jump anywhere
    interrupted = true;

    ERR_ASSERT(ERR((err_errno_t) pthread_cond_signal(&interrupted_cond),
        "could not notify the child thread of the termination"));

pthread_create_fail:
    {
        err_t join_error = ERR((err_errno_t) pthread_join(child_thread_id, NULL),
            "failed to join the child thread");

        if (ERR_FAILED(join_error)) {
            err_log_free(LOG_WARN, &join_error);
        }
    }

    pthread_cond_destroy(&interrupted_cond);
    pthread_condattr_destroy(&interrupted_cond_attr);
    pthread_mutex_destroy(&mtx);
    pthread_mutexattr_destroy(&mtx_attr);

    for (dlist_str_node_t *node = dlist_str_head_mut(&list);
            node != NULL;
            node = dlist_str_next_mut(node)) {
        string_free(dlist_str_get_mut(node));
    }

    dlist_str_free(&list);

    if (ERR_FAILED(error)) {
        err_log_free(LOG_ERR, &error);

        return 1;
    }

    pthread_exit(0);
}
