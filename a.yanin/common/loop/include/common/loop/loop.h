#pragma once

#include <stdatomic.h>
#include <stdbool.h>

#include <pthread.h>

#include <common/error.h>
#include <common/executor/executor.h>
#include <common/posix/io.h>

typedef struct handler handler_t;

#define ARC_LABEL handler
#define ARC_ELEMENT_TYPE handler_t
#define ARC_CONFIG (COLLECTION_DECLARE)
#define ARC_FREE_CB handler_free
#include <common/memory/arc.h>

typedef arc_handler_t *arc_handler_ptr_t;

#define VEC_LABEL handler
#define VEC_ELEMENT_TYPE arc_handler_ptr_t
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

typedef struct loop loop_t;

typedef enum {
    LOOP_READ = POLLIN,
    LOOP_WRITE = POLLOUT,
    LOOP_ERR = POLLERR,
    LOOP_HUP = POLLHUP,

    LOOP_ALL_IN = LOOP_READ | LOOP_WRITE,
    LOOP_ALL = LOOP_READ | LOOP_WRITE | POLLERR | POLLHUP,
} poll_flags_t;

typedef enum {
    LOOP_HANDLER_READY,
    LOOP_HANDLER_QUEUED,
    LOOP_HANDLER_UNREGISTERED,
} loop_handler_status_t;

typedef void (*handler_vtable_free_t)(handler_t *self);
typedef error_t *(*handler_vtable_process_t)(handler_t *self, loop_t *loop, poll_flags_t events);
typedef error_t *(*handler_vtable_on_error_t)(handler_t *self, loop_t *loop, error_t *error);
typedef void (*handler_on_free_cb_t)(handler_t *self);

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
    void *custom_data;
    loop_t *loop;
    handler_on_free_cb_t on_free;
    int fd;
    _Atomic(loop_handler_status_t) status;
    // passive handles don't block the loop from stopping even if they are
    // still registered
    bool passive;
    atomic_bool force;

    // the following fields are protected by `mtx`
    pthread_mutex_t mtx;
    poll_flags_t current_flags;
    poll_flags_t pending_flags;
};

// Create a new instance of `loop_t`.
error_t *loop_new(executor_t *executor, loop_t **result);

// Frees the resources allocated by `self`.
//
// The loop must have been stopped.
// No task must be running at the point of the call.
void loop_free(loop_t *self);

// Registers a handler in the loop.
//
// The ownership over the handler is transferred to the loop.
error_t *loop_register(loop_t *self, handler_t *handler);

// Starts the loop.
//
// The loop is run until it has no registered handlers or is aborted by
// a failed handler.
//
// If the loop was aborted by a handler, returns the error returned by the
// handler.
error_t *loop_run(loop_t *self);

// Unregisters all the handlers from the loop and interrupts the loop.
//
// See `handler_unregister`.
//
// This function is async-signal-safe.
void loop_stop(loop_t *self);

// Forcibly interrupts the next (or the current) iteration of the loop.
void loop_interrupt(loop_t *self);

// Initializes the `handle_t` struct.
//
// Must be called by handler implementations during their initialization.
void handler_init(handler_t *self, handler_vtable_t const *vtable, int fd);

// Frees a handler.
//
// The handler is freed in the following fashion:
// 1. First, if the handler has an `on_free` callback set, it's invoked.
// 2. The `free` method specified in the vtable is called.
// 3. The base handler struct is freed.
// 4. `free(self)` is called.
void handler_free(handler_t *self);

// Unregisters a handler from a loop.
//
// If this is called during an iteration of the loop, its unregistration is
// deferred until the end of the iteration.
// If it had any pending events, the handle will still process them during the
// iteration.
//
// When the handler is unregistered, it is freed and cannot be used afterwards.
//
// The handler must have been registered in the loop prior to this call.
void handler_unregister(handler_t *handler);

// Returns the current event mask.
//
// This method must be called from a synchronized context.
poll_flags_t handler_current_mask(handler_t const *self);

// Returns the event mask to be applied on the next iteration of the loop.
//
// This method must be called from a synchronized context.
poll_flags_t handler_pending_mask(handler_t const *self);

// Set the event mask to be applied on the next iteration of the loop.
//
// This method must be called from a synchronized context.
poll_flags_t handler_set_pending_mask(handler_t *self, poll_flags_t flags);

// Returns a pointer to the loop this handler belongs to.
//
// Returns `NULL` if the handler has not been registered yet.
loop_t *handler_loop(handler_t *self);

// Lock the handler, establishing a synchronized context.
//
// This method must be called from an unsynchronized context.
void handler_lock(handler_t *self);

// Unlock the handler, exiting the synchronized context.
//
// This method must be called from a synchronized context.
void handler_unlock(handler_t *self);

// Returns the fd managed by the handler.
//
// Returns `-1` if the handler has no associated fd.
int handler_fd(handler_t const *self);

// Forces the handler's process method to be called on the next (or current) loop iteration,
// regardless of whether it has any I/O events pending.
void handler_force(handler_t *self);

// Returns the custom data associated with this handler.
//
// The default value is `NULL`.
//
// This method must be called from a synchronized context.
void *handler_custom_data(handler_t const *self);

// Sets the custom data associated with this handler.
//
// Returns the previous value.
//
// This method must be called from a synchronized context.
void *handler_set_custom_data(handler_t *self, void *data);

// Sets a callback to be invoked when this handler is about to be freed.
//
// This can be used to deallocate the resources the custom data points to.
void handler_set_on_free(handler_t *self, handler_on_free_cb_t on_free);
