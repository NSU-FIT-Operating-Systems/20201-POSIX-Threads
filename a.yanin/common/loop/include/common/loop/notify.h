#pragma once

#include <common/loop/loop.h>

// A basic notification mechanism.
//
// You could think of this as a kind of atomic flag that works with `loop_t`.
// Posting a notification raises it; once the notification callback is invoked, the flag is reset.
// Accordingly, posting multiple notifications schedules only one invocation.
typedef struct notify notify_t;

// The notification callback.
typedef error_t *(*notify_cb_t)(loop_t *loop, notify_t *notify);

// Creates a new instance of `notify_t`.
error_t *notify_new(notify_t **result);

// Posts a notification on `self`.
//
// Returns `true` if `self` had no unprocessed notifications prior to this call.
//
// `self` must have already been registered in the loop.
bool notify_post(notify_t *self);

// Wakes up the instance of `notify_t` unconditionally.
//
// This triggers an event on the associated fd, but does not post any notification, meaning the
// callback will not be invoked.
// The ordering between posted-notification wakeups and unconditional wakeups is unspecified.
// If multiple unconditional wakeups are requested, some of them may be dropped.
// The instance will be woken up at least once, however.
//
// This method is async-signal-safe.
void notify_wakeup(notify_t *self);

// Sets the notification callback.
//
// If `on_notified` is `NULL` (the default value), no notification will be processed.
//
// Must be called from a synchronized context.
void notify_set_cb(notify_t *self, notify_cb_t on_notified);
