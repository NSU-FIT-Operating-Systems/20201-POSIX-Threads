#include "common/loop/notify.h"

#include <assert.h>

#include <common/posix/adapter.h>
#include <common/posix/ipc.h>
#include <common/posix/file.h>

#include "util.h"

struct notify {
    handler_t handler;
    notify_cb_t on_notified;

    pthread_mutex_t mtx;
    int wr_fd;

    bool raised;
};

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
    bool was_raised = self->raised;

    if (flags & LOOP_READ) {
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

    self->raised = false;
    assert_mutex_unlock(&self->mtx);

    if (err) goto read_fail;

    if (was_raised && self->on_notified) {
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

error_t *notify_new(notify_t **result) {
    error_t *err = NULL;

    notify_t *self = calloc(1, sizeof(notify_t));
    err = error_wrap("Could not allocate memory", OK_IF(self != NULL));
    if (err) goto calloc_fail;

    int rd_fd = -1;
    int wr_fd = -1;
    err = error_from_posix(wrapper_pipe(&rd_fd, &wr_fd));
    if (err) goto pipe_fail;

    err = error_wrap("Could not switch the read end to non-blocking mode", error_from_posix(
        wrapper_fcntli(rd_fd, F_SETFL, O_NONBLOCK)));
    if (err) goto fcntli_fail;

    err = error_wrap("Could not switch the write end to non-blocking mode", error_from_posix(
        wrapper_fcntli(wr_fd, F_SETFL, O_NONBLOCK)));
    if (err) goto fcntli_fail;

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

    *result = self;

    return err;

fcntli_fail:
pipe_fail:
    free(self);

calloc_fail:
    return err;
}

bool notify_post(notify_t *self) {
    assert_mutex_lock(&self->mtx);
    bool was_raised = self->raised;

    if (!was_raised) {
        ssize_t written_count = -1;
        posix_err_t status = wrapper_write(self->wr_fd, "1", 1, &written_count);

        if (status.errno_code != EWOULDBLOCK && status.errno_code != EAGAIN) {
            error_assert(error_from_posix(status));
            error_assert(OK_IF(written_count == 1));
        }
    }

    self->raised = true;
    assert_mutex_unlock(&self->mtx);

    return !was_raised;
}

void notify_wakeup(notify_t *self) {
    ssize_t written_count = -1;
    posix_err_t status = wrapper_write(self->wr_fd, "2", 1, &written_count);

    if (status.errno_code != EWOULDBLOCK && status.errno_code != EAGAIN) {
        error_assert(error_from_posix(status));
        error_assert(OK_IF(written_count == 1));
    }
}

void notify_set_cb(notify_t *self, notify_cb_t on_notified) {
    self->on_notified = on_notified;

    if (on_notified == NULL) {
        handler_set_pending_mask(&self->handler, 0);
    } else {
        handler_set_pending_mask(&self->handler, LOOP_READ);
    }
}
