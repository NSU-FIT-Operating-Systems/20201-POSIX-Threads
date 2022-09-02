#include <assert.h>
#include <string.h>

#include <pthread.h>

#include <common/log/log.h>

#include "error.h"

enum {
    THREAD_COUNT = 4,
};

static char const *const *const lines[THREAD_COUNT] = {
    (char const *const []) {
        "thread-1: line 1",
        "thread-1: line 2",
        "thread-1: line 3",
        "thread-1: line 4",
        "thread-1: line 5",
        "thread-1: line 6",
        "thread-1: line 7",
        "thread-1: line 8",
        "thread-1: line 9",
        "thread-1: line 10",
        "thread-1: line 11",
        "thread-1: line 12",
        NULL,
    },
    (char const *const []) {
        "thread-2: line 1",
        "thread-2: line 2",
        "thread-2: line 3",
        "thread-2: line 4",
        "thread-2: line 5",
        "thread-2: line 6",
        "thread-2: line 7",
        "thread-2: line 8",
        "thread-2: line 9",
        "thread-2: line 10",
        "thread-2: line 11",
        "thread-2: line 12",
        NULL,
    },
    (char const *const []) {
        "thread-3: line 1",
        "thread-3: line 2",
        "thread-3: line 3",
        "thread-3: line 4",
        "thread-3: line 5",
        "thread-3: line 6",
        "thread-3: line 7",
        "thread-3: line 8",
        "thread-3: line 9",
        "thread-3: line 10",
        "thread-3: line 11",
        "thread-3: line 12",
        NULL,
    },
    (char const *const []) {
        "thread-4: line 1",
        "thread-4: line 2",
        "thread-4: line 3",
        "thread-4: line 4",
        "thread-4: line 5",
        "thread-4: line 6",
        "thread-4: line 7",
        "thread-4: line 8",
        "thread-4: line 9",
        "thread-4: line 10",
        "thread-4: line 11",
        "thread-4: line 12",
        NULL,
    },
};

static void print_usage(void) {
    fputs("Usage: thread-print\n", stderr);
}

static void *child_thread(void *strings_opaque) {
    char const *const *str = strings_opaque;

    while (*str != NULL) {
        puts(*(str++));
    }

    return 0;
}

int main(int argc, char *[]) {
    if (argc != 1) {
        print_usage();

        return 1;
    }

    err_t error = OK;

    pthread_t thread_ids[THREAD_COUNT];
    int created_thread_count = 0;

    for (; created_thread_count < THREAD_COUNT; ++created_thread_count) {
        error = ERR((err_errno_t) pthread_create(
            &thread_ids[created_thread_count], NULL,
            child_thread, (void *) lines[created_thread_count]
        ), "failed to start a child thread");

        if (ERR_FAILED(error)) goto pthread_create_fail;
    }

pthread_create_fail:
    for (int i = 0; i < created_thread_count; ++i) {
        err_t join_error = ERR((err_errno_t) pthread_join(thread_ids[i], NULL),
            "failed to join a child thread");

        if (ERR_FAILED(join_error)) {
            err_log_free(LOG_WARN, &join_error);
        }
    }

    if (ERR_FAILED(error)) {
        err_log_free(LOG_ERR, &error);

        return 1;
    }

    pthread_exit(0);
}
