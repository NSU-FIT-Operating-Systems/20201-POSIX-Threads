#pragma once

#include <common/executor/executor.h>

// A multi-threaded executor with a fixed-size thread pool.
//
// Tasks submitted to the executor are added to a queue.
typedef struct executor_thread_pool executor_thread_pool_t;

typedef error_t *(*executor_thread_pool_on_error_cb_t)(
    executor_thread_pool_t *self,
    error_t *err,
    task_t task
);

// Creates a new thread pool executor with the given `size`.
//
// The threads are spawn immediately; if any thread fails to start, an error is returned.
error_t *executor_thread_pool_new(
    char const *pool_name,
    size_t size,
    executor_thread_pool_t **result
);

// Sets the callback to invoke when a task submitted to the executor returns an error.
//
// If `on_error` is `NULL`, which is the default, such an error aborts the process.
void executor_thread_pool_on_error(
    executor_thread_pool_t *self,
    executor_thread_pool_on_error_cb_t on_error
);
