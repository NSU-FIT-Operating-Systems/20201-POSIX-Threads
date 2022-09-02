#include <assert.h>
#include <string.h>

#include <pthread.h>

#include <common/log/log.h>

#include "error.h"

static void print_usage(void) {
    fputs("Usage: thread-create\n", stderr);
}

static void *child_thread(void *) {
    for (int i = 0; i < 10; ++i) {
        printf("child line %d\n", i);
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

    if (ERR_FAILED(error)) {
        err_log_free(LOG_ERR, &error);

        return 1;
    }

    for (int i = 0; i < 10; ++i) {
        printf("main line %d\n", i);
    }

    pthread_exit(0);
}
