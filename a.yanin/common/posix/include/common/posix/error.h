#pragma once

#include <errno.h>
#include <stddef.h>

typedef struct {
    int errno_code;
    char const *message;
} posix_err_t;

static inline posix_err_t make_posix_err_ok() {
    return (posix_err_t) {
        .errno_code = 0,
        .message = NULL,
    };
}

static inline posix_err_t make_posix_err(char const *message) {
    return (posix_err_t) {
        .errno_code = errno,
        .message = message,
    };
}

