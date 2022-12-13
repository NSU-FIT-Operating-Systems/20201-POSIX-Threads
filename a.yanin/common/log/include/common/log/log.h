#pragma once

#include <stdarg.h>

#define LOG_MODE_UNSYNC 0
#define LOG_MODE_SYNC 1

#if LOG_MODE == LOG_MODE_UNSYNC

static inline void log_lock(void) {}
static inline void log_unlock(void) {}

#else

static pthread_mutex_t log_mtx = PTHREAD_MUTEX_INITIALIZER;

static inline void log_lock(void) {
    pthread_mutex_lock(&log_mtx);
}

static inline void log_unlock(void) {
    pthread_mutex_unlock(&log_mtx);
}

#endif

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERR,
} log_level_t;

void log_vprintf_impl(log_level_t level, char const *fmt, va_list args);
void log_write_impl(log_level_t level, char const *str);
void log_vwritef_impl(log_level_t level, char const *fmt, va_list args);

[[gnu::format(printf, 2, 3)]]
static inline void log_printf(log_level_t level, char const *fmt, ...) {
    log_lock();

    va_list args;
    va_start(args, fmt);
    log_vprintf_impl(level, fmt, args);

    log_unlock();
}

static inline void log_write(log_level_t level, char const *str) {
    log_lock();
    log_write_impl(level, str);
    log_unlock();
}

static inline void log_vwritef(log_level_t level, char const *fmt, va_list args) {
    log_lock();
    log_vwritef_impl(level, fmt, args);
    log_unlock();
}
