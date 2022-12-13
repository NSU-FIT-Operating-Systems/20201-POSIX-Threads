#include "common/loop/notify.h"

#include "util.h"

static error_t *notify_on_read(loop_t *loop, pipe_handler_rd_t *rd, slice_t) {
    notify_t *self = handler_custom_data((handler_t *) rd);
    self->raised = false;

    // FIXME: `self` is passed to `on_notify` while `self->rd` is already locked
    // this means it's pretty much unusable in the callback
    // (a possible way to resolve this is to build on top of pipe fds directly, forgoing the pipe
    // handler approach — I think it's worthwhile...)
    return self->on_notified(loop, self);
}

error_t *notify_init(notify_t *self) {
    error_t *err = pipe_handler_new(&self->wr, &self->rd);
    if (err) goto fail;

    handler_set_custom_data((handler_t *) self->rd, self);
    handler_set_custom_data((handler_t *) self->wr, self);

    self->on_notified = (notify_cb_t) { NULL };
    self->custom_data = NULL;
    self->raised = false;

fail:
    return err;
}

error_t *notify_register(notify_t *self, loop_t *loop) {
    error_t *err = NULL;

    bool rd_owned = true;
    err = loop_register(loop, (handler_t *) self->rd);
    if (err) goto rd_register_fail;

    rd_owned = false;

    err = loop_register(loop, (handler_t *) self->wr);
    if (err) goto wr_register_fail;

    return err;

wr_register_fail:

rd_register_fail:
    if (rd_owned) {
        handler_free((handler_t *) self->rd);
    } else {
        loop_unregister(loop, (handler_t *) self->rd);
    }

    handler_free((handler_t *) self->wr);

    return err;
}

void notify_unregister(notify_t *self, loop_t *loop) {
    loop_unregister(loop, (handler_t *) self->wr);
    loop_unregister(loop, (handler_t *) self->rd);
}

bool notify_post(notify_t *self) {
    static char data[] = { 1 };
    static slice_t slice = { .base = data, .len = 1};

    handler_lock((handler_t *) self->rd);
    bool was_raised = self->raised;

    if (!was_raised) {
        handler_lock((handler_t *) self->wr);
        error_assert(pipe_write(self->wr, 1, &slice, NULL, NULL));
        handler_unlock((handler_t *) self->wr);
    }

    self->raised = true;
    handler_unlock((handler_t *) self->rd);

    return !was_raised;
}

void notify_set_cb(notify_t *self, notify_cb_t on_notified) {
    handler_lock((handler_t *) self->rd);
    self->on_notified = on_notified;
    pipe_read(self->rd, on_notified ? notify_on_read : NULL, NULL);
    handler_unlock((handler_t *) self->rd);
}

void *notify_custom_data(notify_t const *self) {
    handler_lock((handler_t *) self->rd);
    void *data = self->custom_data;
    handler_unlock((handler_t *) self->rd);

    return data;
}

void *notify_set_custom_data(notify_t *self, void *data) {
    handler_lock((handler_t *) self->rd);
    void *prev = self->custom_data;
    self->custom_data = data;
    handler_unlock((handler_t *) self->rd);

    return prev;
}
