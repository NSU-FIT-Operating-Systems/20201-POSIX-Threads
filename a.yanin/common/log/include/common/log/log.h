#pragma once

#include <common/config.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <threads.h>

// when included from another project, the header is not present regardless of the value of the
// option
#ifndef COMMON_PTHREADS_DISABLED
#include <pthread.h>

extern pthread_mutex_t log_mtx;
#endif

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERR,
    LOG_FATAL,
} log_level_t;

typedef void (*log_hook_t)(log_level_t level);

// The log hook is called each time a log function is called.
// By default, it prints the level prefix.
extern thread_local log_hook_t log_hook;

extern char const *const log_prefix_debug;
extern char const *const log_prefix_info;
extern char const *const log_prefix_warn;
extern char const *const log_prefix_err;
extern char const *const log_prefix_fatal;
char const *log_prefix_for_level(log_level_t log_level);

bool log_is_sync(void);
#ifndef COMMON_PTHREADS_DISABLED
void log_set_sync(void);
#endif

log_level_t log_get_level(void);
void log_set_level(log_level_t log_level);

void log_vprintf_impl(log_level_t level, char const *fmt, va_list args);
void log_write_impl(log_level_t level, char const *str);
void log_vwritef_impl(log_level_t level, char const *fmt, va_list args);

static inline bool log_level_filtered(log_level_t level) {
    return level < log_get_level();
}

[[gnu::format(printf, 2, 3)]]
[[maybe_unused]]
static inline void log_printf(log_level_t level, char const *fmt, ...) {
    if (log_level_filtered(level)) return;

#ifndef COMMON_PTHREADS_DISABLED
    bool sync = log_is_sync();

    if (sync) {
        pthread_mutex_lock(&log_mtx);
    }
#endif

    va_list args;
    va_start(args, fmt);
    log_vprintf_impl(level, fmt, args);

#ifndef COMMON_PTHREADS_DISABLED
    if (sync) {
        pthread_mutex_unlock(&log_mtx);
    }
#endif
}

[[maybe_unused]]
static inline void log_write(log_level_t level, char const *str) {
    if (log_level_filtered(level)) return;

#ifndef COMMON_PTHREADS_DISABLED
    bool sync = log_is_sync();

    if (sync) {
        pthread_mutex_lock(&log_mtx);
    }
#endif

    log_write_impl(level, str);

#ifndef COMMON_PTHREADS_DISABLED
    if (sync) {
        pthread_mutex_unlock(&log_mtx);
    }
#endif
}

[[maybe_unused]]
static inline void log_vwritef(log_level_t level, char const *fmt, va_list args) {
    if (log_level_filtered(level)) return;

#ifndef COMMON_PTHREADS_DISABLED
    bool sync = log_is_sync();

    if (sync) {
        pthread_mutex_lock(&log_mtx);
    }
#endif

    log_vwritef_impl(level, fmt, args);

#ifndef COMMON_PTHREADS_DISABLED
    if (sync) {
        pthread_mutex_unlock(&log_mtx);
    }
#endif
}

[[gnu::format(printf, 1, 2)]]
noreturn void log_abort(char const *fmt, ...);
