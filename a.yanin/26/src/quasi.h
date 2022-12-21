#pragma once

#include <stdatomic.h>

#include <pthread.h>

#include <common/collections/string.h>

#include "error.h"

typedef enum {
    QUASI_STATE_OK,
    QUASI_STATE_DROPPED,
} quasi_state_t;

/// A fixed-capacity string message queue.
typedef struct {
    string_t *storage;
    size_t wr_idx;
    size_t rd_idx;
    size_t capacity;
    bool full;
    size_t blocked_count;
    quasi_state_t state;
    pthread_mutex_t mtx;
    pthread_cond_t rd_cond;
    pthread_cond_t wr_cond;
    pthread_cond_t drop_cond;
} quasi_t;

// Creates a new message queue with the specified capacity.
err_t quasi_new(size_t capacity, quasi_t *result);

// Frees the memory used by `self`.
// There must be no producers or consumers blocked on quasi_push or quasi_pop.
void quasi_free(quasi_t *self);

// Clears the queue, dropping all read/write operations currently blocked on this queue, if any.
void quasi_drop(quasi_t *self);

// Pushes a `message` to the queue, blocking if the size cap is reached.
//
// If `quasi_drop` is called while the current thread is blocked, the operation fails and
// the function returns `QUASI_STATE_DROPPED`.
// Otherwise `QUASI_STATE_OK` is returned.
quasi_state_t quasi_push(quasi_t *self, string_t message);

// Pops a message from the queue to `result`, blocking if the queue is empty.
//
// If `quasi_drop` is called which the current thread is blocked, the operation fails and
// the function returns `QUASI_STATE_DROPPED`.
// Otherwise `QUASI_STATE_OK` is returned.
quasi_state_t quasi_pop(quasi_t *self, string_t *result);
