#include <assert.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

#include <common/log/log.h>
#include <common/posix/time.h>

#include "error.h"

static void print_usage(void) {
    fputs("Usage: thread-cancel\n", stderr);
}

static void *child_thread(void *) {
    while (true) {
        puts("text");
    }

    return 0;
}

int main(int argc, char *[]) {
    if (argc != 1) {
        print_usage();

        return 1;
    }

    err_t error = OK;

    pthread_t thread_id;
    error = ERR((err_errno_t) pthread_create(&thread_id, NULL, child_thread, NULL),
        "failed to start a child thread");
    if (ERR_FAILED(error)) goto pthread_create_fail;

    err_t sleep_error = ERR(wrapper_nanosleep(&(struct timespec) { .tv_sec = 2 }),
        "failed to sleep for two seconds");

    if (ERR_FAILED(sleep_error)) {
        err_log_free(LOG_WARN, &sleep_error);
    }

    error = ERR((err_errno_t) pthread_cancel(thread_id), "failed to cancel the child thread");

    err_t join_error = ERR((err_errno_t) pthread_join(thread_id, NULL),
        "failed to join the child thread");

    if (ERR_FAILED(join_error)) {
        err_log_free(LOG_WARN, &sleep_error);
    }

pthread_create_fail:
    if (ERR_FAILED(error)) {
        err_log_free(LOG_ERR, &error);

        return 1;
    }

    pthread_exit(0);
}
