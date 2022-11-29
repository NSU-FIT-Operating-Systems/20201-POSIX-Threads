#pragma once

#include <stdarg.h>

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERR,
} log_level_t;

[[gnu::format(printf, 2, 3)]]
void log_printf(log_level_t level, char const *fmt, ...);

void log_write(log_level_t level, char const *str);
void log_vwritef(log_level_t level, char const *fmt, va_list args);
