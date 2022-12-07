#include "common/loop/loop.h"

#include <stdlib.h>

#include <common/error.h>

#include "util.h"

void handler_init(handler_t *self, handler_vtable_t const *vtable, int fd) {
    self->vtable = vtable;
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

    self->vtable->free(self);
    free(self);
}

poll_flags_t handler_current_mask(handler_t const *self) {
    return self->current_flags;
}

poll_flags_t *handler_pending_mask(handler_t *self) {
    return &self->pending_flags;
}

void handler_lock(handler_t *self) {
    assert_mutex_lock(&self->mtx);
}

void handler_unlock(handler_t *self) {
    assert_mutex_unlock(&self->mtx);
}
