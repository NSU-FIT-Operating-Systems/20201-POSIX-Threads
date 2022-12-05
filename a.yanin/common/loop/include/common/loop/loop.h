#pragma once

#include <stdatomic.h>
#include <stdbool.h>

#include <pthread.h>

#include <common/error.h>
#include <common/posix/io.h>

#include "common/loop/executor.h"

// TODO:
// - store arc_handler_t
// - keep a reference in the tasks
typedef struct handler handler_t;
typedef handler_t *handlerp_t;

#define VEC_LABEL handler
#define VEC_ELEMENT_TYPE handlerp_t
#define VEC_CONFIG (COLLECTION_DECLARE)
#include <common/collections/vec.h>

#define VEC_LABEL pollfd
#define VEC_ELEMENT_TYPE struct pollfd
#define VEC_CONFIG (COLLECTION_DECLARE)
#include <common/collections/vec.h>

typedef error_t *errorp_t;

#define VEC_LABEL error
#define VEC_ELEMENT_TYPE errorp_t
#define VEC_CONFIG (COLLECTION_DECLARE)
#include <common/collections/vec.h>

typedef struct loop {
    vec_handler_t handlers;
    pthread_mutex_t pending_mtx;
    vec_handler_t pending_handlers;
    pthread_mutex_t error_mtx;
    vec_error_t errors;
    executor_t *executor;
    atomic_bool stopped;
} loop_t;

typedef enum {
    LOOP_READ = POLLIN,
    LOOP_WRITE = POLLOUT,
} poll_flags_t;

typedef enum {
    LOOP_HANDLER_READY,
    LOOP_HANDLER_QUEUED,
    LOOP_HANDLER_UNREGISTERED,
} loop_handler_status_t;

typedef void (*handler_vtable_free_t)(handler_t *self);
typedef error_t *(*handler_vtable_process_t)(handler_t *self, poll_flags_t events);
typedef error_t *(*handler_vtable_on_error_t)(handler_t *self, error_t *error);

typedef struct {
    // Frees the resources associated with the handler.
    handler_vtable_free_t free;

    // Processes events that have occured in the fd managed by this handler.
    handler_vtable_process_t process;

    // Called when the handler returns an error.
    //
    // Gives an opportunity to capture the error.
    // If this callback returns an error, the loop is aborted with that error.
    //
    // This vtable entry can be `NULL`, in which case all errors cause the
    // handler to be unregistered whichout aborting the loop.
    handler_vtable_on_error_t on_error;
} handler_vtable_t;

// The base struct of an event handler.
//
// Concrete implementations must include this struct as the first field
// to allow upcasting.
//
// The handler manages a single fd and responds to events happening on it.
//
// Some of the handler methods require they be called from a synchronized
// context.
// This means that they can only be called if any of the following is true:
// - the handler has not been registered in an event loop yet
// - the call occurs during the execution of one of the handler's methods
// - the call occurs in a critical section started by `handler_lock`
//
// If none of the above applies, the context is unsynchronized.
struct handler {
    handler_vtable_t const *vtable;
    int fd;
    _Atomic(loop_handler_status_t) active;

    // the following fields are protected by `mtx`
    pthread_mutex_t mtx;
    poll_flags_t current_flags;
    poll_flags_t pending_flags;
};

// Initializes a new instance of `loop_t`.
void loop_init(loop_t *self, executor_t *executor);

// Registers a handler in the loop.
//
// The ownership over the handler is transferred to the loop.
error_t *loop_register(loop_t *self, handler_t *handler);

// Unregisters a handler from the loop.
//
// If this is called during an iteration of the loop, its unregistration is
// deferred until the end of the iteration.
// If if had any pending events, the handle will still process them during the
// iteration.
//
// When the handler is unregistered, it is freed and cannot be used afterwards.
//
// The handler must have been registered in the loop prior to this call.
void loop_unregister(loop_t *self, handler_t *handler);

// Starts the loop.
//
// The loop is run until it has no registered handlers or is aborted by
// a failed handler.
//
// If the loop was aborted by a handler, returns the error returned by the
// handler.
error_t *loop_run(loop_t *self);

// Unregisters all the handlers from the loop.
//
// See `loop_unregister`.
void loop_stop(loop_t *self);

// Initializes the `handle_t` struct.
//
// Must be called by handler implementations during their initialization.
void handler_init(handler_t *self, handler_vtable_t const *vtable, int fd);

// Frees a handler.
//
// Also dispatches to the `free` method in the handle vtable.
void handler_free(handler_t *self);

// Returns the current event mask.
//
// This method must be called from a synchronized context.
poll_flags_t handler_current_mask(handler_t const *self);

// Returns the event mask to be applied on the next iteration of the loop.
//
// This method must be called from a synchronized context.
poll_flags_t *handler_pending_mask(handler_t *self);

// Lock the handler, establishing a synchronized context.
//
// This method must be called from an unsynchronized context.
void handler_lock(handler_t *self);

// Unlock the handler, exiting the synchronized context.
//
// This method must be called from a synchronized context.
void handler_unlock(handler_t *self);