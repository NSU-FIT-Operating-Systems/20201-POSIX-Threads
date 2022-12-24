#include "common/loop/loop.h"

#include <stdlib.h>

#include <common/error.h>

#include "util.h"

void handler_init(handler_t *self, handler_vtable_t const *vtable, int fd) {
    if (fd < 0) {
        fd = -1;
    }

    self->vtable = vtable;
    self->custom_data = NULL;
    self->on_free = NULL;
    self->loop = NULL;
    self->fd = fd;
    self->status = LOOP_HANDLER_READY;
    self->passive = false;
    self->current_flags = 0;
    self->pending_flags = 0;

    pthread_mutexattr_t mtx_attr;
    error_assert(error_wrap("Could not initialize mutex attributes",
        error_from_errno(pthread_mutexattr_init(&mtx_attr))));
    pthread_mutexattr_settype(&mtx_attr, PTHREAD_MUTEX_ERRORCHECK);

    error_assert(error_wrap("Could not create a mutex", error_from_errno(
        pthread_mutex_init(&self->mtx, &mtx_attr))));

    pthread_mutexattr_destroy(&mtx_attr);
}

void handler_free(handler_t *self) {
    if (self == NULL) return;

    if (self->on_free != NULL) {
        self->on_free(self);
    }

    self->vtable->free(self);
    free(self);
}

void handler_unregister(handler_t *handler) {
    handler->status = LOOP_HANDLER_UNREGISTERED;
}

poll_flags_t handler_current_mask(handler_t const *self) {
    return self->current_flags;
}

poll_flags_t handler_pending_mask(handler_t const *self) {
    return self->pending_flags;
}

poll_flags_t handler_set_pending_mask(handler_t *self, poll_flags_t flags) {
    poll_flags_t prev = flags;
    self->pending_flags = flags;

    if (prev != flags && self->loop != NULL) {
        loop_interrupt(self->loop);
    }

    return prev;
}

loop_t *handler_loop(handler_t *self) {
    return self->loop;
}

void handler_lock(handler_t *self) {
    log_printf(LOG_DEBUG, "Locking handler %p", (void *) self);
    assert_mutex_lock(&self->mtx);
}

void handler_unlock(handler_t *self) {
    log_printf(LOG_DEBUG, "Unlocking handler %p", (void *) self);
    assert_mutex_unlock(&self->mtx);
}

int handler_fd(handler_t const *self) {
    return self->fd;
}

void handler_force(handler_t *self) {
    self->force = true;

    if (self->loop != NULL) {
        loop_interrupt(self->loop);
    }
}

void *handler_custom_data(handler_t const *self) {
    return self->custom_data;
}

void *handler_set_custom_data(handler_t *self, void *data) {
    void *prev = self->custom_data;
    self->custom_data = data;

    return prev;
}

void handler_set_on_free(handler_t *self, handler_on_free_cb_t on_free) {
    self->on_free = on_free;
}
