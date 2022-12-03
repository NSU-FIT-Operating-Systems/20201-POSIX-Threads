#include "common/log/log.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>

#include <pthread.h>

pthread_mutex_t log_mtx = PTHREAD_MUTEX_INITIALIZER;

#define ESC "\x1b"
#define CSI ESC "["
#define SGR "m"
#define SGR_BOLD "1"
#define SGR_RED "31"
#define SGR_YELLOW "33"
#define SGR_CYAN "36"
#define SGR_BRIGHT_BLACK "90"

char const *const log_prefix_debug = CSI SGR_BOLD SGR CSI SGR_BRIGHT_BLACK SGR "DEBUG" CSI SGR;
char const *const log_prefix_info = CSI SGR_BOLD SGR CSI SGR_CYAN SGR "INFO" CSI SGR;
char const *const log_prefix_warn = CSI SGR_BOLD SGR CSI SGR_YELLOW SGR "WARN" CSI SGR;
char const *const log_prefix_err = CSI SGR_BOLD SGR CSI SGR_RED SGR "ERR" CSI SGR;

static void log_print_prefix(log_level_t level) {
    char const *prefix = NULL;

    switch (level) {
    case LOG_DEBUG:
        prefix = log_prefix_debug;
        break;

    case LOG_INFO:
        prefix = log_prefix_info;
        break;

    case LOG_WARN:
        prefix = log_prefix_warn;
        break;

    case LOG_ERR:
        prefix = log_prefix_err;
        break;
    }

    fprintf(stderr, "%s: ", prefix);
}

thread_local log_hook_t log_hook = log_print_prefix;

static atomic_bool is_sync = false;

bool log_is_sync(void) {
    return is_sync;
}

void log_set_sync(void) {
    is_sync = true;
}

static _Atomic(log_level_t) log_level = LOG_DEBUG;

log_level_t log_get_level(void) {
    return log_level;
}

void log_set_level(log_level_t level) {
    log_level = level;
}

void log_vprintf_impl(log_level_t level, char const *fmt, va_list args) {
    log_hook(level);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
}

void log_write_impl(log_level_t level, char const *str) {
    log_hook(level);
    fputs(str, stderr);
}

void log_vwritef_impl(log_level_t level, char const *fmt, va_list args) {
    log_hook(level);
    vfprintf(stderr, fmt, args);
}
