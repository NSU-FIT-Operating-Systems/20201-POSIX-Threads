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

    // can be `NULL`, check `host_null`
    slice_t host;

    // can be `NULL`, check `port_null`
    uint16_t port;

    slice_t path;

    // can be `NULL`, check `query_null`
    slice_t query;

    // can be `NULL`, check `fragment_null`
    slice_t fragment;

    bool host_null;
    bool port_null;
    bool query_null;
    bool fragment_null;
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
error_t *url_parse(slice_t slice, url_t *result, bool *fatal);
