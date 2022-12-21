#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "error.h"

err_t write_all(int fd, unsigned char const *buf, size_t size);

// buf_t: a dynamically-sized heap-allocated byte buffer with independent read and write cursors
typedef struct {
    char *buf;
    size_t capacity;
    size_t read_end;
    size_t write_pos;
    size_t read_pos;
} buf_t;

err_t buf_new(size_t capacity, buf_t *result);
void buf_free(buf_t *self);

err_t buf_resize(buf_t *self, size_t new_capacity);

// these writes and reads are all-or-nothing: there are no partial reads nor writes
// if count is larger than the corresponding size available, these methods return an error
// writes perform compactfying if necessary
err_t buf_write(buf_t *self, char const *buf, size_t count);
err_t buf_write_u8(buf_t *self, uint8_t value);
err_t buf_write_u16_be(buf_t *self, uint16_t value);
err_t buf_write_u32_be(buf_t *self, uint32_t value);
err_t buf_write_u64_be(buf_t *self, uint64_t value);

err_t buf_read(buf_t *self, char *buf, size_t count);
err_t buf_read_u8(buf_t *self, uint8_t *buf);
err_t buf_read_u16_be(buf_t *self, uint16_t *buf);
err_t buf_read_u32_be(buf_t *self, uint32_t *buf);
err_t buf_read_u64_be(buf_t *self, uint64_t *buf);

// if count < avilable_write_size, resizes the buffer
err_t buf_ensure_enough_write_space(buf_t *self, size_t count);

err_t buf_write_resizing(buf_t *self, char const *buf, size_t count);
err_t buf_write_u8_resizing(buf_t *self, uint8_t value);
err_t buf_write_u16_be_resizing(buf_t *self, uint16_t value);
err_t buf_write_u32_be_resizing(buf_t *self, uint32_t value);
err_t buf_write_u64_be_resizing(buf_t *self, uint64_t value);

// resets the available read size to 0
void buf_reset(buf_t *self);

void buf_read_seek(buf_t *self, ssize_t offset);

// moves the unread data to the beginning of the buffer
void buf_compact(buf_t *self);

// returns the available write space size (without compaction)
size_t buf_immediately_available_write_size(buf_t const *self);

// same as above, but takes into account the possibility of compaction
size_t buf_available_write_size(buf_t const *self);

size_t buf_available_read_size(buf_t const *self);

char const *buf_get_read_ptr(buf_t const *self);
char *buf_get_write_ptr(buf_t *self);

size_t buf_capacity(buf_t const *self);

// ring_buf_t: a fixed-size heap-allocated circular byte buffer
typedef struct {
    char *buf;
    size_t capacity;
    size_t begin;
    size_t end;
} ring_buf_t;

err_t ring_buf_new(size_t capacity, ring_buf_t *result);
void ring_buf_free(ring_buf_t *self);

size_t ring_buf_available_write_size(ring_buf_t const *self);
size_t ring_buf_available_read_size(ring_buf_t const *self);

size_t ring_buf_immediately_available_write_size(ring_buf_t const *self);
size_t ring_buf_immediately_available_read_size(ring_buf_t const *self);

char *ring_buf_get_write_ptr(ring_buf_t *self, size_t *immediately_available);
char *ring_buf_get_read_ptr(ring_buf_t const *self, size_t *immediately_available);

size_t ring_buf_commit_write(ring_buf_t *self, size_t count);
size_t ring_buf_commit_read(ring_buf_t *self, size_t count);

size_t ring_buf_write_from(ring_buf_t *self, char const *buf, size_t count);
size_t ring_buf_read_to(ring_buf_t *self, char *buf, size_t count);
