#pragma once

#include <stdarg.h>
#include <stdbool.h>

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

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_DEBUG
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

static inline bool log_level_filtered(log_level_t level) {
    return level < LOG_LEVEL;
}

[[gnu::format(printf, 2, 3)]]
[[maybe_unused]]
static inline void log_printf(log_level_t level, char const *fmt, ...) {
    if (log_level_filtered(level)) return;

    log_lock();

    va_list args;
    va_start(args, fmt);
    log_vprintf_impl(level, fmt, args);

    log_unlock();
}

[[maybe_unused]]
static inline void log_write(log_level_t level, char const *str) {
    if (log_level_filtered(level)) return;

    log_lock();
    log_write_impl(level, str);
    log_unlock();
}

[[maybe_unused]]
static inline void log_vwritef(log_level_t level, char const *fmt, va_list args) {
    if (log_level_filtered(level)) return;

    log_lock();
    log_vwritef_impl(level, fmt, args);
    log_unlock();
}
