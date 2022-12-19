#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <common/loop/io.h>

typedef struct url {
    char *buf;
    slice_t scheme;
    slice_t username;
    slice_t password;

    // can be NULL
    slice_t host;

    // if the port is null, stores 0xffff'ffff
    uint32_t port;

    slice_t path;

    // can be NULL
    slice_t query;

    // can be NULL
    slice_t fragment;
} url_t;

bool url_eq(url_t const *lhs, url_t const *rhs);
int url_copy(url_t const *url, url_t *result);
