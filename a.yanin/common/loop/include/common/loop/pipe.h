#pragma once

#include <common/loop/io.h>
#include <common/loop/loop.h>

// A handler of the write end of a pipe.
// Can be safely upcast to `handler_t`.
//
// All the methods that take `self` as a parameter must be called from a synchronized context
// (unless stated otherwise).
//
// Freeing the handler closes the write end's file descriptor.
typedef struct pipe_handler_wr pipe_handler_wr_t;

// A handler of the read end of a pipe.
// Can be safely upcast to `handler_t`.
//
// All the methods that take `self` as a parameter must be called from a synchronized context
// (unless stated otherwise).
//
// Freeing the handler closes the read end's file descriptor.
typedef struct pipe_handler_rd pipe_handler_rd_t;

// Called if a write operation on the pipe fails.
typedef error_t *(*pipe_wr_on_error_cb_t)(
    loop_t *loop,
    pipe_handler_wr_t *handler,
    error_t *err,
    size_t written_count
);

// Called if a read operation on the pipe fails.
typedef error_t *(*pipe_rd_on_error_cb_t)(loop_t *loop, pipe_handler_rd_t *handler, error_t *err);

// Called when the pipe has received data from the other end.
typedef error_t *(*pipe_on_read_cb_t)(loop_t *loop, pipe_handler_rd_t *handler, slice_t buf);

// Called when a write operation on the pipe succeeds.
typedef error_t *(*pipe_on_write_cb_t)(loop_t *loop, pipe_handler_wr_t *handler);

// Creates a new pipe and two handlers to manage its two ends.
error_t *pipe_handler_new(pipe_handler_wr_t **writer, pipe_handler_rd_t **reader);

// Sets the callback to invoke whenever the pipe receives data.
//
// If `on_read` is `NULL`, which is the default, the handler stops reading any data from the pipe.
//
// The `on_error` callback can also be specified for custom error handling or left `NULL` to make
// the handler fail in such case.
void pipe_read(pipe_handler_rd_t *self, pipe_on_read_cb_t on_read, pipe_rd_on_error_cb_t on_error);

// Returns `true` if all the data has been read from the pipe and no more will be received.
//
// The `eof` flag is set before the last call to the `on_read` callback.
bool pipe_is_eof(pipe_handler_rd_t const *self);

// Creates a new request to write to the pipe.
//
// The request is added to the queue.
// Once the previously made request have been processed, the handler will attempt at write the data
// from the `slices`.
// If multiple slices are provided, they are concatenated in array order.
//
// When all the data has been written, the `on_write` callback is invoked (unless `NULL`).
//
// if an error has been encountered while writing data (possibly from a previous request),
// the `on_error` callback is invoked with the count of bytes already written passed to it,
// unless `on_error` is `NULL`.
error_t *pipe_write(
    pipe_handler_wr_t *self,
    size_t slice_count,
    slice_t const slices[static slice_count],
    pipe_on_write_cb_t on_write,
    pipe_wr_on_error_cb_t on_error
);
