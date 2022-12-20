#pragma once

#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include <common/error.h>
#include <common/loop/io.h>

#define TODO(MSG) (abort(), (void) MSG)

[[maybe_unused]]
static inline void assert_mutex_lock(pthread_mutex_t *mtx) {
    error_assert(error_wrap("Could not lock a mutex", error_from_errno(
        pthread_mutex_lock(mtx))));
}

[[maybe_unused]]
static inline void assert_mutex_unlock(pthread_mutex_t *mtx) {
    error_assert(error_wrap("Could not unlock a mutex", error_from_errno(
        pthread_mutex_unlock(mtx))));
}

[[maybe_unused]]
static bool slice_cmp(slice_t lhs, slice_t rhs) {
    if (lhs.len < rhs.len) {
        return -1;
    } else if (lhs.len > rhs.len) {
        return 1;
    } else {
        return memcmp(lhs.base, rhs.base, lhs.len);
    }
}

[[maybe_unused]]
static slice_t rebase_slice(char const *base, char *new_base, slice_t slice) {
    return (slice_t) {
        .base = base == NULL ? NULL : new_base + (slice.base - base),
        .len = slice.len,
    };
}

[[maybe_unused]]
static slice_t slice_from_cstr(char const *str) {
    return (slice_t) {
        .base = str,
        .len = strlen(str),
    };
}

[[maybe_unused]]
static slice_t slice_empty(void) {
    return (slice_t) {
        .base = NULL,
        .len = 0,
    };
}
