#pragma once

#include <stdlib.h>

#ifndef COMMON_PTHREADS_DISABLED
#include <pthread.h>
#endif

#include <common/error.h>

#define TODO(MSG) (abort(), (void) MSG)

#ifndef COMMON_PTHREADS_DISABLED
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
#endif
