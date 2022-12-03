#include "common/error.h"

static void error_format_indent(size_t level, string_t *buf) {
    if (level == 0) return;
    string_appendf(buf, "\n%*s", (int)(level * 2), "");
}

static void error_format_backtrace(backtrace_t const *backtrace, size_t level, string_t *buf) {
    string_t prefix_str;
    char const *prefix = "\n  at ";
    bool prefix_allocated = false;

    if (string_new(&prefix_str) == COMMON_ERROR_CODE_OK) {
        error_format_indent(level + 1, &prefix_str);
        string_appendf(&prefix_str, "at ");
        prefix = string_as_cptr(&prefix_str);
        prefix_allocated = true;
    }

    backtrace_format(backtrace, prefix, buf);

    if (prefix_allocated) {
        string_free(&prefix_str);
    }
}

static void error_format_impl(
    error_t const *self,
    error_verbosity_t verbosity,
    string_t *buf,
    size_t level
) {
    if (self == NULL) {
        if (level == 0) {
            string_appendf(buf, "success");
        }

        return;
    }

    if (self->vtable->description != NULL) {
        error_format_indent(level, buf);
        self->vtable->description(self, buf);

        if (verbosity & ERROR_VERBOSITY_BACKTRACE) {
            error_format_backtrace(self->backtrace, level, buf);
        }
    }

    if (self->vtable->description == NULL || (verbosity & ERROR_VERBOSITY_SOURCE_CHAIN)) {
        size_t primary_level = level + (self->vtable->description == NULL ? 0 : 1);
        error_format_impl(self->vtable->source(self), verbosity, buf, primary_level);
    }

    if (verbosity & ERROR_VERBOSITY_SOURCE_CHAIN) {
        error_format_impl(self->vtable->secondary(self), verbosity, buf, level + 1);
    }
}

void error_format(error_t const *self, error_verbosity_t verbosity, string_t *buf) {
    error_format_impl(self, verbosity, buf, 0);
}

void error_log_free(error_t **self, log_level_t log_level, error_verbosity_t verbosity) {
    string_t buf;

    if (string_new(&buf) != COMMON_ERROR_CODE_OK) {
        log_printf(LOG_ERR, "Could not allocate a buffer for an error message");
        error_free(self);

        return;
    }

    error_format(*self, verbosity, &buf);
    log_printf(log_level, "%s", string_as_cptr(&buf));
    string_free(&buf);
    error_free(self);
}
