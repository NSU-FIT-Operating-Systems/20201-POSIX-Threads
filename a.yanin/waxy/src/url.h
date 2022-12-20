#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <common/loop/io.h>
#include <common/error.h>

typedef struct url {
    string_t buf;
    slice_t scheme;
    slice_t username;
    slice_t password;

    // can be NULL
    slice_t host;

    // if the port is null, stores 0xffff'ffff (-1)
    uint32_t port;

    slice_t path;

    // can be NULL
    slice_t query;

    // can be NULL
    slice_t fragment;
} url_t;

// Compares two `url_t` instances for equality.
bool url_eq(url_t const *lhs, url_t const *rhs);

// Copies a `url_t` into `result`.
error_t *url_copy(url_t const *url, url_t *result);

// Parses input to `result`.
//
// The URL standard specifies some fields as nullable, disctinct from empty.
// Such fields are represented as slices of negative length.
//
// Assumes the input is valid UTF-8.
// It won't break if it's not, but it may percent-encode bytes incorrectly.
error_t *url_parse(slice_t slice, url_t *result);

[[maybe_unused]]
static inline bool slice_is_null(slice_t slice) {
    return slice.len < 0;
}
