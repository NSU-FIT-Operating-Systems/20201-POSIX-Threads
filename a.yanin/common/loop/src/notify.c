#include "common/loop/notify.h"

#include <assert.h>

#include <common/posix/adapter.h>
#include <common/posix/ipc.h>

#include "util.h"

static void notify_free(notify_t *self) {
    error_t *err = NULL;
    pthread_mutex_destroy(&self->mtx);
    err = error_from_posix(wrapper_close(self->wr_fd));

    if (err) {
        error_log_free(&err, LOG_WARN, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE);
    }

    err = error_from_posix(wrapper_close(handler_fd(&self->handler)));

    if (err) {
        error_log_free(&err, LOG_WARN, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE);
    }
}

static error_t *notify_process(notify_t *self, loop_t *loop, poll_flags_t flags) {
    error_t *err = NULL;

    assert_mutex_lock(&self->mtx);

    if (self->raised) {
        assert(flags & LOOP_READ);
        char buf = 0;
        ssize_t read_count = -1;

        err = error_from_posix(wrapper_read(
            handler_fd(&self->handler),
            &buf,
            1,
            &read_count
        ));
        err = error_combine(err, OK_IF(read_count == 1));
    }

    bool was_raised = self->raised;
    self->raised = false;
    assert_mutex_unlock(&self->mtx);

    if (err) goto read_fail;

    if (was_raised) {
        err = self->on_notified(loop, self);

        if (err) goto cb_fail;
    }

cb_fail:
read_fail:
    return err;
}

static handler_vtable_t const notify_vtable = {
    .free = (handler_vtable_free_t) notify_free,
    .process = (handler_vtable_process_t) notify_process,
    .on_error = NULL,
};

error_t *notify_init(notify_t *self) {
    error_t *err = NULL;

    int rd_fd = -1;
    int wr_fd = -1;
    err = error_from_posix(wrapper_pipe(&rd_fd, &wr_fd));
    if (err) goto fail;

    pthread_mutexattr_t mtx_attr;
    error_assert(error_wrap("Could not intiialize mutex attributes", error_from_errno(
        pthread_mutexattr_init(&mtx_attr))));
    pthread_mutexattr_settype(&mtx_attr, PTHREAD_MUTEX_ERRORCHECK);
    error_assert(error_wrap("Could not initialize a mutex", error_from_errno(
        pthread_mutex_init(&self->mtx, &mtx_attr))));
    pthread_mutexattr_destroy(&mtx_attr);

    handler_init(&self->handler, &notify_vtable, rd_fd);
    self->wr_fd = wr_fd;
    self->on_notified = (notify_cb_t) { NULL };
    self->raised = false;

fail:
    return err;
}

bool notify_post(notify_t *self) {
    assert_mutex_lock(&self->mtx);
    bool was_raised = self->raised;

    if (!was_raised) {
        ssize_t written_count = -1;
        error_assert(error_from_posix(wrapper_write(self->wr_fd, "1", 1, &written_count)));
        error_assert(OK_IF(written_count == 1));
    }

    self->raised = true;
    assert_mutex_unlock(&self->mtx);

    return !was_raised;
}

void notify_set_cb(notify_t *self, notify_cb_t on_notified) {
    self->on_notified = on_notified;

    if (on_notified == NULL) {
        *handler_pending_mask(&self->handler) = 0;
    } else {
        *handler_pending_mask(&self->handler) = LOOP_READ;
    }
}
