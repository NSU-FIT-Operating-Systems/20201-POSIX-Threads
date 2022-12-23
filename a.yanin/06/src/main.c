#include <assert.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

#include <common/io.h>
#include <common/log/log.h>
#include <common/posix/time.h>

#include "error.h"

#define VEC_ELEMENT_TYPE pthread_t
#define VEC_LABEL thread
#define VEC_CONFIG (COLLECTION_DECLARE | COLLECTION_DEFINE | COLLECTION_STATIC)
#include <common/collections/vec.h>

static const long long sleep_per_char_ms = 10;

static void print_usage(void) {
    fputs("Usage: sleepsort\n", stderr);
}

static void thread_cleanup(void *opaque_string) {
    assert(opaque_string != NULL);

    string_free(opaque_string);
    free(opaque_string);
}

static void *sleep_and_print(void *opaque_string) {
    assert(opaque_string != NULL);

    pthread_cleanup_push(thread_cleanup, opaque_string);
    string_t *str = opaque_string;
    size_t len = string_len(str);
    long long sleep_time_ms = sleep_per_char_ms * (long long) len;
    struct timespec sleep_time = {
        .tv_sec = sleep_time_ms / 1'000,
        .tv_nsec = (sleep_time_ms % 1'000) * 1'000'000,
    };

    err_t sleep_error = ERR(wrapper_nanosleep(&sleep_time), "failed to put a thread to sleep");

    if (ERR_FAILED(sleep_error)) {
        err_log_free(LOG_WARN, &sleep_error);
    }

    puts(string_as_cptr(str));

    pthread_cleanup_pop(1);

    return 0;
}

static err_t dispatch_line(vec_thread_t *threads, bool *eof) {
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

    pthread_t thread_id;
    error = ERR((err_errno_t) pthread_create(&thread_id, NULL, sleep_and_print, line),
        "failed to create a child thread");
    if (ERR_FAILED(error)) goto pthread_create_fail;

    error = ERR(vec_thread_push(threads, thread_id), "failed to store the id of a child thread");
    if (ERR_FAILED(error)) goto vec_thread_push_fail;

    return error;

vec_thread_push_fail:
    pthread_cancel(thread_id);
    err_t join_error = ERR((err_errno_t) pthread_join(thread_id, NULL),
        "failed to join a canceled thread");

    if (ERR_FAILED(join_error)) {
        err_log_free(LOG_WARN, &join_error);
    }

    return error;

pthread_create_fail:
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

    vec_thread_t threads = vec_thread_new();
    bool eof = false;

    do {
        error = ERR(dispatch_line(&threads, &eof), "failed to dispatch a line from stdin");
    } while (!eof && !ERR_FAILED(error));

    for (size_t i = 0; i < vec_thread_len(&threads); ++i) {
        pthread_t thread_id = *vec_thread_get(&threads, i);
        err_t join_error = ERR((err_errno_t) pthread_join(thread_id, NULL),
            "failed to join a child thread");

        if (ERR_FAILED(join_error)) {
            err_log_free(LOG_WARN, &join_error);
        }
    }

    vec_thread_free(&threads);

    if (ERR_FAILED(error)) {
        err_log_free(LOG_ERR, &error);

        return 1;
    }

    pthread_exit(0);
}
