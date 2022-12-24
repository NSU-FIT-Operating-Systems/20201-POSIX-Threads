#pragma once

#include <sys/uio.h>

#include <common/loop/io.h>
#include <common/loop/loop.h>

#define VEC_ELEMENT_TYPE struct iovec
#define VEC_LABEL iovec
#define VEC_CONFIG (COLLECTION_DECLARE)
#include <common/collections/vec.h>

size_t iov_max_size(void);

typedef struct {
    slice_t const *slices;
    size_t slice_count;
    size_t written_count;
} write_req_t;

typedef enum {
    IO_PROCESS_FINISHED,
    IO_PROCESS_PARTIAL,
    IO_PROCESS_AGAIN,
} io_process_result_t;

error_t *io_process_write_req(
    void *self,
    loop_t *loop,
    error_t *err,
    io_process_result_t *processed,
    int fd,
    write_req_t *(*get_req)(void *self),
    error_t *(*on_write)(void *self, loop_t *loop, write_req_t *req),
    error_t *(*on_error)(void *self, loop_t *loop, write_req_t *req, error_t *err)
);
