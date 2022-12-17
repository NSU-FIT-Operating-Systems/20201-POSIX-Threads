#include "io.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <common/posix/io.h>

#include "util/byte-order.h"

err_t write_all(int fd, unsigned char const *buf, size_t size) {
    err_t error = OK;

    unsigned char const *cursor = buf;
    unsigned char const *end = buf + size;

    while (cursor < end) {
        ssize_t written = 0;

        if (ERR_FAILED(error = ERR(wrapper_write(fd, cursor, end - cursor, &written), NULL))) {
            return error;
        }

        cursor += written;
    }

    return error;
}

// buffer

err_t buf_new(size_t capacity, buf_t *result) {
    char *buf = malloc(capacity);

    err_t error = ERR((bool)(buf != NULL), "could not allocate memory for a buffer");

    if (ERR_FAILED(error)) {
        return error;
    }

    *result = (buf_t) {
        .buf = buf,
        .capacity = capacity,
        .read_end = 0,
        .write_pos = 0,
        .read_pos = 0,
    };

    return error;
}

void buf_free(buf_t *self) {
    free(self->buf);
    self->buf = NULL;
    self->capacity = 0;
    self->read_end = 0;
    self->write_pos = 0;
    self->read_pos = 0;
}

err_t buf_resize(buf_t *self, size_t new_capacity) {
    // might consider amortizing memory allocation

    if (new_capacity == 0) {
        buf_free(self);

        return OK;
    }

    err_t error = OK;

    size_t read_size = buf_available_read_size(self);

    if (ERR_FAILED(error = ERR((bool)(read_size <= new_capacity),
            "resizing would truncate data"))) {
        return error;
    }

    char *buf = NULL;

    if (self->read_pos != 0) {
        buf = malloc(new_capacity);
    } else {
        buf = realloc(self->buf, new_capacity);
    }

    if (ERR_FAILED(error = ERR((bool)(buf != NULL), "could not allocate memory for the buffer"))) {
        return error;
    }

    if (self->read_pos != 0) {
        memcpy(buf, self->buf + self->read_pos, read_size);
    }

    self->buf = buf;
    self->capacity = new_capacity;

    self->write_pos -= self->read_pos;
    self->read_pos = 0;
    self->read_end = read_size;

    return error;
}

err_t buf_write(buf_t *self, char const *buf, size_t count) {
    err_t error = ERR((bool)(count <= buf_available_write_size(self)),
        "there was not enough space for writing into the buffer");

    if (ERR_FAILED(error)) {
        return error;
    }

    if (buf != NULL) {
        if (buf_immediately_available_write_size(self) < count) {
            buf_compact(self);
        }

        memcpy(buf_get_write_ptr(self), buf, count);
    }

    self->write_pos += count;

    if (self->read_end < self->write_pos) {
        self->read_end = self->write_pos;
    }

    return error;
}

err_t buf_write_u8(buf_t *self, uint8_t value) {
    return ERR(buf_write(self, (char const *) &value, 1), "failed to write a u8 into the buffer");
}

err_t buf_write_u16_be(buf_t *self, uint16_t value) {
    return ERR(buf_write(self, (char const *) &(uint16_t) {u16_to_be(value)}, 2),
        "failed to write a u16 into the buffer");
}

err_t buf_write_u32_be(buf_t *self, uint32_t value) {
    return ERR(buf_write(self, (char const *) &(uint32_t) {u32_to_be(value)}, 4),
        "failed to write a u32 into the buffer");
}

err_t buf_write_u64_be(buf_t *self, uint64_t value) {
    return ERR(buf_write(self, (char const *) &(uint64_t) {u64_to_be(value)}, 8),
        "failed to write a u64 into the buffer");
}

err_t buf_read(buf_t *self, char *buf, size_t count) {
    err_t error = ERR((bool)(count <= buf_available_read_size(self)),
        "requested a read of more bytes than was written to the buffer");

    if (ERR_FAILED(error)) {
        return error;
    }

    if (buf != NULL) {
        memcpy(buf, buf_get_read_ptr(self), count);
    }

    self->read_pos += count;

    if (self->read_pos == self->read_end) {
        self->read_pos = 0;
        self->write_pos = 0;
        self->read_end = 0;
    }

    return error;
}

err_t buf_read_u8(buf_t *self, uint8_t *buf) {
    return ERR(buf_read(self, (char *) buf, 1), "failed to read a u8 from the buffer");
}

err_t buf_read_u16_be(buf_t *self, uint16_t *buf) {
    uint16_t value_be = -1;

    err_t error = ERR(buf_read(self, (char *) &value_be, 2),
        "failed to read a u16 from the buffer");

    if (!ERR_FAILED(error)) {
        *buf = u16_from_be(value_be);
    }

    return error;
}

err_t buf_read_u32_be(buf_t *self, uint32_t *buf) {
    uint32_t value_be = -1;

    err_t error = ERR(buf_read(self, (char *) &value_be, 4),
        "failed to read a u32 from the buffer");

    if (!ERR_FAILED(error)) {
        *buf = u32_from_be(value_be);
    }

    return error;
}

err_t buf_read_u64_be(buf_t *self, uint64_t *buf) {
    uint64_t value_be = -1;

    err_t error = ERR(buf_read(self, (char *) &value_be, 8),
        "failed to read a u64 from the buffer");

    if (!ERR_FAILED(error)) {
        *buf = u64_from_be(value_be);
    }

    return error;
}

err_t buf_ensure_enough_write_space(buf_t *self, size_t count) {
    err_t error = OK;

    if (buf_available_write_size(self) < count) {
        error = ERR(buf_resize(self, buf_available_read_size(self) + count), NULL);
    }

    return error;
}

err_t buf_write_resizing(buf_t *self, char const *buf, size_t count) {
    err_t error = OK;

    if (!ERR_FAILED(error = ERR(buf_ensure_enough_write_space(self, count),
            "failed to ensure enough available write space"))) {
        error = ERR(buf_write(self, buf, count), NULL);
    }

    return error;
}

err_t buf_write_u8_resizing(buf_t *self, uint8_t value) {
    err_t error = OK;

    if (!ERR_FAILED(error = ERR(buf_ensure_enough_write_space(self, 1),
            "failed to ensure enough available write space"))) {
        error = ERR(buf_write_u8(self, value), NULL);
    }

    return error;
}

err_t buf_write_u16_be_resizing(buf_t *self, uint16_t value) {
    err_t error = OK;

    if (!ERR_FAILED(error = ERR(buf_ensure_enough_write_space(self, 2),
            "failed to ensure enough available write space"))) {
        error = ERR(buf_write_u16_be(self, value), NULL);
    }

    return error;
}

err_t buf_write_u32_be_resizing(buf_t *self, uint32_t value) {
    err_t error = OK;

    if (!ERR_FAILED(error = ERR(buf_ensure_enough_write_space(self, 4),
            "failed to ensure enough available write space"))) {
        error = ERR(buf_write_u32_be(self, value), NULL);
    }

    return error;
}

err_t buf_write_u64_be_resizing(buf_t *self, uint64_t value) {
    err_t error = OK;

    if (!ERR_FAILED(error = ERR(buf_ensure_enough_write_space(self, 8),
            "failed to ensure enough available write space"))) {
        error = ERR(buf_write_u64_be(self, value), NULL);
    }

    return error;
}

void buf_reset(buf_t *self) {
    assert(self != NULL);

    self->read_end = 0;
    self->read_pos = 0;
    self->write_pos = 0;
}

void buf_read_seek(buf_t *self, ssize_t offset) {
    assert(self != NULL);

    if (offset < 0 && self->read_pos < SSIZE_MAX && offset < -(ssize_t) self->read_pos) {
        self->read_pos = 0;
    } else if (offset > 0 && (size_t) offset > buf_available_read_size(self)) {
        self->read_pos = self->read_end;
    } else {
        self->read_pos += offset;
    }
}

void buf_compact(buf_t *self) {
    assert(self != NULL);

    if (self->read_pos == 0) {
        return;
    }

    size_t read_size = buf_available_read_size(self);
    memmove(self->buf, self->buf + self->read_pos, read_size);
    self->read_end = read_size;
    self->write_pos -= self->read_pos;
    self->read_pos = 0;
}

size_t buf_immediately_available_write_size(buf_t const *self) {
    return self->capacity - self->write_pos;
}

size_t buf_available_write_size(buf_t const *self) {
    return buf_immediately_available_write_size(self) + self->read_pos;
}

size_t buf_available_read_size(buf_t const *self) {
    return self->read_end - self->read_pos;
}

char const *buf_get_read_ptr(buf_t const *self) {
    return self->buf + self->read_pos;
}

char *buf_get_write_ptr(buf_t *self) {
    return self->buf + self->write_pos;
}

size_t buf_capacity(buf_t const *self) {
    return self->capacity;
}

// ring buffer

err_t ring_buf_new(size_t capacity, ring_buf_t *result) {
    err_t error = OK;

    char *buf = malloc(capacity);

    if (!ERR_FAILED(error = ERR((bool)(buf != NULL),
            "could not allocate memory for the ring buffer"))) {
        *result = (ring_buf_t) {
            .buf = NULL,
            .capacity = 0,
            .begin = 0,
            .end = 0,
        };
    }

    return error;
}

void ring_buf_free(ring_buf_t *self) {
    free(self->buf);

    *self = (ring_buf_t) {
        .buf = NULL,
        .capacity = 0,
        .begin = 0,
        .end = 0,
    };
}

size_t ring_buf_available_write_size(ring_buf_t const *self) {
    size_t size = ring_buf_immediately_available_write_size(self);

    if (self->end >= self->begin) {
        size += self->begin;
    }

    return size;
}

size_t ring_buf_available_read_size(ring_buf_t const *self) {
    size_t size = ring_buf_immediately_available_read_size(self);

    if (self->end < self->begin) {
        size += self->end;
    }

    return size;
}

size_t ring_buf_immediately_available_write_size(ring_buf_t const *self) {
    if (self->end < self->begin) {
        return self->begin - self->end - 1;
    }

    return self->capacity - self->end - 1;
}

size_t ring_buf_immediately_available_read_size(ring_buf_t const *self) {
    if (self->end < self->begin) {
        return self->capacity - self->begin;
    }

    return self->end - self->begin;
}

char *ring_buf_get_write_ptr(ring_buf_t *self, size_t *immediately_available) {
    if (immediately_available != NULL) {
        *immediately_available = ring_buf_immediately_available_write_size(self);
    }

    return self->buf + self->end;
}

char *ring_buf_get_read_ptr(ring_buf_t const *self, size_t *immediately_available) {
    if (immediately_available != NULL) {
        *immediately_available = ring_buf_immediately_available_read_size(self);
    }

    return self->buf + self->begin;
}

size_t ring_buf_commit_write(ring_buf_t *self, size_t count) {
    size_t imm_available = ring_buf_immediately_available_write_size(self);
    size_t commited_size = count;

    if (imm_available < count) {
        commited_size = imm_available;
    }

    self->end += commited_size;
    self->end %= self->capacity;

    return commited_size;
}

size_t ring_buf_commit_read(ring_buf_t *self, size_t count) {
    size_t imm_available = ring_buf_immediately_available_read_size(self);
    size_t commited_size = count;

    if (imm_available < count) {
        commited_size = imm_available;
    }

    self->begin += commited_size;
    self->begin %= self->capacity;

    if (self->begin == self->end) {
        self->begin = 0;
        self->end = 0;
    }

    return commited_size;
}
