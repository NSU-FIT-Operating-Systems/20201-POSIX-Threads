#pragma once

#include <stdlib.h>

#include <pthread.h>

#include <common/error.h>

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
static inline void assert_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mtx) {
    error_assert(error_wrap("Could not wait on a condition variable", error_from_errno(
        pthread_cond_wait(cond, mtx))));
}
