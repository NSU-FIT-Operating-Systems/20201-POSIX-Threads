#pragma once

#include <common/loop/loop.h>
#include <common/loop/pipe.h>

typedef struct notify notify_t;

// The notification callback.
typedef error_t *(*notify_cb_t)(loop_t *loop, notify_t *notify);

// A basic notification mechanism.
//
// You could think of this as a kind of atomic flag that works with `loop_t`.
// Posting a notification raises it; once the notification callback is invoked, the flag is reset.
// Accordingly, posting multiple notifications schedules only one invocation.
//
// NOTE: `notify_t` cannot be cast to `handler_t` and must be registered via `notify_register`!
struct notify {
    pipe_handler_wr_t *wr;
    pipe_handler_rd_t *rd;

    // protected by rd's mtx
    notify_cb_t on_notified;
    void *custom_data;
    bool raised;
};

// Initializes a new instance of `notify_t`.
error_t *notify_init(notify_t *self);

// Registers `self` in the `loop`.
//
// If an error occurs, the resources allocated by `notify_init` are freed.
// The instance has to be re-initialized afterwards.
error_t *notify_register(notify_t *self, loop_t *loop);

// Unregisters `self` from the `loop`.
void notify_unregister(notify_t *self, loop_t *loop);

// Posts a notification on `self`.
//
// Returns `true` if `self` had no unprocessed notifications prior to this call.
//
// `self` must have already been registered in the loop.
bool notify_post(notify_t *self);

// Sets the notification callback.
//
// If `on_notified` is `NULL` (the default value), no notification will be processed.
void notify_set_cb(notify_t *self, notify_cb_t on_notified);

// Returns the custom data associated with `self`.
//
// The default value is `NULL`.
void *notify_custom_data(notify_t const *self);

// Associates custom data with `self`.
//
// Returns the previous value.
void *notify_set_custom_data(notify_t *self, void *data);
