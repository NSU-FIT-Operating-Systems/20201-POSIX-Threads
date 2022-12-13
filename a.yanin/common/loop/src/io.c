#include "io.h"

#include <pthread.h>

#include <common/error.h>
#include <common/error-codes/adapter.h>
#include <common/posix/adapter.h>
#include <common/posix/io.h>
#include <common/posix/proc.h>

#define VEC_ELEMENT_TYPE struct iovec
#define VEC_LABEL iovec
#define VEC_CONFIG (COLLECTION_DEFINE)
#include <common/collections/vec.h>

static size_t iov_max = 0;
static pthread_once_t iov_max_once = PTHREAD_ONCE_INIT;

static void iov_max_init() {
    long iov_size = -1;
    error_t *err = error_wrap(
        "Could not request the value of _SC_IOV_MAX",
        error_from_posix(wrapper_sysconf(_SC_IOV_MAX, &iov_size))
    );
    if (err) {
        error_log_free(&err, LOG_WARN, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE);
    } else if (iov_size > 0) {
        iov_max = (size_t) iov_size;

        return;
    }

    iov_max = 16;
}

size_t iov_max_size(void) {
    error_assert(error_from_errno(pthread_once(&iov_max_once, iov_max_init)));

    return iov_max;
}

error_t *io_process_write_req(
    void *self,
    loop_t *loop,
    error_t *err,
    bool *processed,
    int fd,
    write_req_t *(*get_req)(void *self),
    error_t *(*on_write)(void *self, loop_t *loop, write_req_t *req),
    error_t *(*on_error)(void *self, loop_t *loop, write_req_t *req, error_t *err)
) {
    write_req_t *req = get_req(self);

    if (err) {
        err = error_wrap("A previous write request has failed", err);

        goto chained_error;
    }

    size_t first_unfinished_idx = 0;
    size_t written_count = req->written_count;

    for (; first_unfinished_idx < req->slice_count; ++first_unfinished_idx) {
        if (written_count < req->slices[first_unfinished_idx].len) {
            break;
        }

        written_count -= req->slices[first_unfinished_idx].len;
    }

    size_t iov_max = iov_max_size();
    size_t iov_size = req->slice_count - first_unfinished_idx;

    if (iov_size > iov_max) {
        iov_size = iov_max;
    }

    vec_iovec_t iov = vec_iovec_new();
    err = error_from_common(vec_iovec_resize(&iov, iov_size));
    if (err) goto iov_resize_fail;

    size_t write_requested = 0;

    for (size_t i = first_unfinished_idx;
            i < req->slice_count && vec_iovec_len(&iov) < iov_size;
            ++i) {
        slice_t slice = req->slices[i];
        void *base = slice.base;
        size_t len = slice.len;

        if (i == first_unfinished_idx) {
            base = (char *) base + written_count;
            len -= written_count;
        }

        if (len == 0) {
            continue;
        }

        write_requested += len;

        vec_iovec_push(&iov, (struct iovec) {
            .iov_base = base,
            .iov_len = len,
        });
    }

    ssize_t count = -1;
    err = error_from_posix(wrapper_writev(
        fd,
        vec_iovec_as_ptr(&iov),
        (int) vec_iovec_len(&iov),
        &count
    ));
    if (err) goto writev_fail;

    written_count = req->written_count += (size_t) count;
    assert(req->written_count < write_requested);

    if (req->written_count == write_requested) {
        err = on_write(self, loop, req);
        if (err) goto cb_fail;
    }

    *processed = true;

cb_fail:
writev_fail:
    vec_iovec_free(&iov);

iov_resize_fail:
chained_error:
    if (err) {
        err = on_error(self, loop, req, err);
    }

    if (err) {
        *processed = true;
    }

    return err;
}
