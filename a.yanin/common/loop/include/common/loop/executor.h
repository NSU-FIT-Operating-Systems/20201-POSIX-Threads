#pragma once

#include <common/error.h>

// Represents the result of task submission.
typedef enum {
    // The task has been successfully submitted to the executor queue.
    EXECUTOR_SUBMITTED,

    // The task was dropped because the executor has been shut down.
    EXECUTOR_DROPPED,
} executor_submission_t;

typedef error_t *(*task_cb_t)(void *data);

typedef struct {
    task_cb_t cb;
    void *data;
} task_t;

typedef struct executor executor_t;

typedef void (*executor_vtable_free_t)(executor_t *self);
typedef executor_submission_t (*executor_vtable_submit_t)(executor_t *self, task_t task);
typedef void (*executor_vtable_shutdown_t)(executor_t *self);

typedef struct {
    // Frees the resources associated with the executor implementation.
    executor_vtable_free_t free;

    // Submits a task to the executor.
    //
    // If the executor has been shut down, returns `EXECUTOR_DROPPED`.
    executor_vtable_submit_t submit;

    // Shuts down the executor.
    //
    // Any tasks submitted afterwards will be rejected.
    executor_vtable_shutdown_t shutdown;
} executor_vtable_t;

struct executor {
    executor_vtable_t const *vtable;
};

// Initializes an `executor_t` struct.
//
// Must be called by executor implementations during their initialization.
void executor_init(executor_t *self, executor_vtable_t const *vtable);

// Frees the executor.
//
// Dispatches to the `free` method in the executor vtable.
void executor_free(executor_t *self);

// Submits a task to the executor.
//
// Dispatches to the `submit` method of the executor vtable.
executor_submission_t executor_submit(executor_t *self, task_t task);

// Shuts down the executor.
//
// Dispatches to the `shutdown` method of the executor vtable.
void executor_shutdown(executor_t *self);
