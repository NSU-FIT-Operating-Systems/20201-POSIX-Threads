#include "common/log/log.h"

#include <stdarg.h>
#include <stdio.h>

#define ESC "\x1b"
#define CSI ESC "["
#define SGR "m"
#define SGR_BOLD "1"
#define SGR_RED "31"
#define SGR_YELLOW "33"
#define SGR_CYAN "36"
#define SGR_BRIGHT_BLACK "90"

static void log_print_prefix(log_level_t level) {
    switch (level) {
    case LOG_DEBUG:
        fputs(CSI SGR_BOLD SGR CSI SGR_BRIGHT_BLACK SGR "DEBUG" CSI SGR ": ", stderr);
        break;

    case LOG_INFO:
        fputs(CSI SGR_BOLD SGR CSI SGR_CYAN SGR "INFO" CSI SGR ": ", stderr);
        break;

    case LOG_WARN:
        fputs(CSI SGR_BOLD SGR CSI SGR_YELLOW SGR "WARN" CSI SGR ": ", stderr);
        break;

    case LOG_ERR:
        fputs(CSI SGR_BOLD SGR CSI SGR_RED SGR "ERR" CSI SGR ": ", stderr);
        break;
    }
}

void log_printf(log_level_t level, char const *fmt, ...) {
    va_list list;
    va_start(list, fmt);
    log_print_prefix(level);
    vfprintf(stderr, fmt, list);
    fputc('\n', stderr);
}

void log_write(log_level_t level, char const *str) {
    log_print_prefix(level);
    fputs(str, stderr);
}

void log_vwritef(log_level_t level, char const *fmt, va_list args) {
    log_print_prefix(level);
    vfprintf(stderr, fmt, args);
}
