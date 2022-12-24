#include "common/loop/pipe.h"

#include <common/error-codes/adapter.h>
#include <common/posix/adapter.h>
#include <common/posix/ipc.h>
#include <common/posix/file.h>

#include "io.h"
#include "src/util.h"

enum {
    READ_BUFFER_SIZE = 16384,
};

typedef struct {
    write_req_t write_req;
    pipe_on_write_cb_t on_write;
    pipe_wr_on_error_cb_t on_error;
} pipe_write_req_t;

#define VEC_ELEMENT_TYPE pipe_write_req_t
#define VEC_LABEL wrreq
#define VEC_CONFIG (COLLECTION_DECLARE | COLLECTION_DEFINE | COLLECTION_STATIC)
#include <common/collections/vec.h>

struct pipe_handler_wr {
    handler_t handler;
    vec_wrreq_t write_reqs;
    pipe_on_write_cb_t on_write;
    pipe_wr_on_error_cb_t on_error;
};

struct pipe_handler_rd {
    handler_t handler;
    pipe_on_read_cb_t on_read;
    pipe_rd_on_error_cb_t on_error;
    bool eof;
};

static void pipe_handler_wr_free(pipe_handler_wr_t *self) {
    error_t *err = error_from_posix(wrapper_close(handler_fd(&self->handler)));

    if (err) {
        error_log_free(&err, LOG_WARN, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE);
    }

    vec_wrreq_free(&self->write_reqs);
}

static write_req_t *pipe_handler_process_write_req_get_req(void *self_opaque) {
    pipe_handler_wr_t *self = self_opaque;

    return &vec_wrreq_get_mut(&self->write_reqs, 0)->write_req;
}

static error_t *pipe_handler_process_write_req_on_write(
    void *self_opaque,
    loop_t *loop,
    write_req_t *req
) {
    pipe_handler_wr_t *self = self_opaque;
    pipe_write_req_t *pipe_req = (pipe_write_req_t *) req;

    if (pipe_req->on_write != NULL) {
        return pipe_req->on_write(loop, self);
    } else {
        return NULL;
    }
}

static error_t *pipe_handler_process_write_req_on_error(
    void *self_opaque,
    loop_t *loop,
    write_req_t *req,
    error_t *err
) {
    pipe_handler_wr_t *self = self_opaque;
    pipe_write_req_t *pipe_req = (pipe_write_req_t *) req;

    if (pipe_req->on_error != NULL) {
        return pipe_req->on_error(loop, self, err, req->written_count);
    } else {
        return err;
    }
}

static error_t *pipe_handler_process_write_req(
    pipe_handler_wr_t *self,
    loop_t *loop,
    error_t *err,
    io_process_result_t *processed
) {
    return io_process_write_req(
        self,
        loop,
        err,
        processed,
        handler_fd(&self->handler),
        pipe_handler_process_write_req_get_req,
        pipe_handler_process_write_req_on_write,
        pipe_handler_process_write_req_on_error
    );
}

static error_t *pipe_handler_wr_process(pipe_handler_wr_t *self, loop_t *loop, poll_flags_t flags) {
    error_t *err = NULL;

    if (!(flags | (LOOP_WRITE | LOOP_ERR))) {
        return err;
    }

    while (vec_wrreq_len(&self->write_reqs) > 0) {
        io_process_result_t processed = false;
        err = pipe_handler_process_write_req(self, loop, err, &processed);

        switch (processed) {
        case IO_PROCESS_FINISHED:
            vec_wrreq_remove(&self->write_reqs, 0);
            [[fallthrough]];

        case IO_PROCESS_PARTIAL:
            if (!err) {
                goto out;
            }

            [[fallthrough]];

        case IO_PROCESS_AGAIN:
            continue;
        }
    }

out:
    if (vec_wrreq_len(&self->write_reqs) == 0) {
        handler_set_pending_mask(&self->handler, 0);
    }

    return err;
}

static void pipe_handler_rd_free(pipe_handler_rd_t *self) {
    error_t *err = error_from_posix(wrapper_close(handler_fd(&self->handler)));

    if (err) {
        error_log_free(&err, LOG_WARN, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE);
    }
}

static error_t *pipe_handler_rd_process(pipe_handler_rd_t *self, loop_t *loop, poll_flags_t flags) {
    error_t *err = NULL;

    if (self->on_read == NULL || !(flags & (LOOP_READ | LOOP_HUP))) {
        return err;
    }

    char *buf = calloc(READ_BUFFER_SIZE, 1);
    err = error_wrap("Could not allocate a buffer for received data", OK_IF(buf != NULL));
    if (err) goto calloc_fail;

    int fd = handler_fd(&self->handler);
    ssize_t read_count = -1;
    err = error_from_posix(wrapper_read(fd, buf, READ_BUFFER_SIZE, &read_count));
    if (err) goto read_fail;

    slice_t slice = {
        .base = buf,
        .len = (size_t) read_count,
    };

    if ((flags & LOOP_HUP) && read_count == 0) {
        self->eof = true;
    }

    err = self->on_read(loop, self, slice);
    if (err) goto cb_fail;

    if (self->eof) {
        poll_flags_t flags = handler_pending_mask(&self->handler);
        flags &= ~LOOP_READ;
        handler_set_pending_mask(&self->handler, flags);
    }

cb_fail:
read_fail:
    free(buf);

calloc_fail:
    if (err && self->on_error != NULL) {
        err = self->on_error(loop, self, err);
    }

    return err;
}

static handler_vtable_t const pipe_handler_wr_vtable = {
    .free = (handler_vtable_free_t) pipe_handler_wr_free,
    .process = (handler_vtable_process_t) pipe_handler_wr_process,
    .on_error = NULL,
};

static handler_vtable_t const pipe_handler_rd_vtable = {
    .free = (handler_vtable_free_t) pipe_handler_rd_free,
    .process = (handler_vtable_process_t) pipe_handler_rd_process,
    .on_error = NULL,
};

error_t *pipe_handler_new(pipe_handler_wr_t **writer, pipe_handler_rd_t **reader) {
    error_t *err = NULL;

    pipe_handler_wr_t *wr = calloc(1, sizeof(pipe_handler_wr_t));
    err = error_wrap("Could not allocate memory for the write end handler", OK_IF(wr != NULL));
    if (err) goto wr_malloc_fail;

    pipe_handler_rd_t *rd = calloc(1, sizeof(pipe_handler_rd_t));
    err = error_wrap("Could not allocate memory for the read end handler", OK_IF(rd != NULL));
    if (err) goto rd_malloc_fail;

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

    handler_init(&wr->handler, &pipe_handler_wr_vtable, wr_fd);
    handler_init(&rd->handler, &pipe_handler_rd_vtable, rd_fd);

    wr->write_reqs = vec_wrreq_new();
    wr->on_error = NULL;
    wr->on_write = NULL;

    rd->on_error = NULL;
    rd->on_read = NULL;
    rd->eof = false;

    *writer = wr;
    *reader = rd;

    return err;

fcntli_fail:
pipe_fail:
    free(rd);

rd_malloc_fail:
    free(wr);

wr_malloc_fail:
    return err;
}

void pipe_read(pipe_handler_rd_t *self, pipe_on_read_cb_t on_read, pipe_rd_on_error_cb_t on_error) {
    self->on_read = on_read;
    self->on_error = on_error;

    if (on_read != NULL) {
        handler_set_pending_mask(&self->handler, LOOP_READ);
    } else {
        handler_set_pending_mask(&self->handler, 0);
    }
}

bool pipe_is_eof(pipe_handler_rd_t const *self) {
    return self->eof;
}

error_t *pipe_write(
    pipe_handler_wr_t *self,
    size_t slice_count,
    slice_t const slices[static slice_count],
    pipe_on_write_cb_t on_write,
    pipe_wr_on_error_cb_t on_error
) {
    error_t *err = error_from_common(vec_wrreq_push(&self->write_reqs, (pipe_write_req_t) {
        .write_req = {
            .slices = slices,
            .slice_count = slice_count,
            .written_count = 0,
        },
        .on_write = on_write,
        .on_error = on_error,
    }));
    if (err) goto fail;

    handler_set_pending_mask(&self->handler, LOOP_WRITE);

fail:
    return err;
}
