#pragma once

#include <common/executor/executor.h>

// A single-threaded, blocking executor.
//
// Any tasks submitted to the executor are executed immediately, blocking the submitter until the
// task finishes.
//
// This executor does not use any synchronization and as such should not be used by multiple threads
// at the same time.
typedef struct executor_single executor_single_t;

typedef error_t *(*executor_single_on_error_cb_t)(executor_single_t *self, error_t *err);

error_t *executor_single_new(char const *name, executor_single_t **result);

// Sets the callback to invoke when a task submitted to the executor returns an error.
//
// If `on_error` is `NULL`, which is the default, such an error aborts the process.
void executor_single_on_error(executor_single_t *self, executor_single_on_error_cb_t on_error);
