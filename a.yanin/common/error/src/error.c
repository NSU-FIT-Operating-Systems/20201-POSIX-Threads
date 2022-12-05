#include "common/error.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void sentinel_error_description(error_t const *, string_t *buf) {
    string_appendf(buf, "<could not allocate memory for the error>");
}

static void sentinel_error_free(error_t *) {}

static error_vtable_t const sentinel_error_vtable = {
    .source = error_null,
    .secondary = error_null,
    .description = sentinel_error_description,
    .free = sentinel_error_free,
};

error_t sentinel_error = {
    .vtable = &sentinel_error_vtable,
    .backtrace = NULL,
};

void error_init(error_t *self, error_vtable_t const *vtable) {
    self->vtable = vtable;
    self->backtrace = backtrace_capture();
}

error_t const *error_null(error_t const *) {
    return NULL;
}

void error_assert(error_t *self) {
    if (self == NULL) return;

    error_log_free(&self, LOG_ERR, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE);
}

typedef struct {
    error_t error;
    error_t *source;
    char const *str;
} error_from_cstr_t;

static void error_from_cstr_description(error_from_cstr_t const *self, string_t *buf) {
    string_appendf(buf, "%s", self->str);
}

static void error_from_cstr_free(error_from_cstr_t *self) {
    error_free(&self->source);
}

static error_vtable_t const error_from_cstr_vtable = {
    .source = error_null,
    .secondary = error_null,
    .description = (error_vtable_description_t) error_from_cstr_description,
    .free = (error_vtable_free_t) error_from_cstr_free,
};

error_t *error_from_cstr(char const *str, error_t *source) {
    error_from_cstr_t *result = malloc(sizeof(error_from_cstr_t));

    if (result == NULL) {
        error_free(&source);

        return &sentinel_error;
    }

    error_init(&result->error, &error_from_cstr_vtable);
    result->str = str;
    result->source = source;

    return (error_t *) result;
}

typedef struct {
    error_t error;
    error_t *source;
    string_t str;
} error_from_string_t;

static void error_from_string_description(error_from_string_t const *self, string_t *buf) {
    string_append(buf, &self->str);
}

static void error_from_string_free(error_from_string_t *self) {
    error_free(&self->source);
    string_free(&self->str);
}

static error_vtable_t const error_from_string_vtable = {
    .source = error_null,
    .secondary = error_null,
    .description = (error_vtable_description_t) error_from_string_description,
    .free = (error_vtable_free_t) error_from_string_free,
};

error_t *error_from_string(string_t str, error_t *source) {
    error_from_string_t *result = malloc(sizeof(error_from_string_t));

    if (result == NULL) {
        error_free(&source);
        string_free(&str);

        return &sentinel_error;
    }

    error_init(&result->error, &error_from_string_vtable);
    result->str = str;
    result->source = source;

    return (error_t *) result;
}

typedef struct {
    error_t error;
    int code;
} error_from_errno_t;

static void error_from_errno_description(error_from_errno_t const *self, string_t *buf) {
    string_appendf(buf, "%s", strerror(self->code));
}

static void error_from_errno_free(error_from_errno_t *) {}

static error_vtable_t const error_from_errno_vtable = {
    .source = error_null,
    .secondary = error_null,
    .description = (error_vtable_description_t) error_from_errno_description,
    .free = (error_vtable_free_t) error_from_errno_free,
};

error_t *error_from_errno(int code) {
    if (code == 0) return NULL;

    error_from_errno_t *result = malloc(sizeof(error_from_errno_t));
    if (result == NULL) return &sentinel_error;

    error_init(&result->error, &error_from_errno_vtable);
    result->code = code;

    return (error_t *) result;
}

typedef struct {
    error_t error;
    char const *expr;
} error_ok_if_t;

static void error_ok_if_description(error_ok_if_t const *self, string_t *buf) {
    string_appendf(buf, "assertion failed: `%s` evaluated to false", self->expr);
}

static void error_ok_if_free(error_ok_if_t *) {}

static error_vtable_t const error_ok_if_vtable = {
    .source = error_null,
    .secondary = error_null,
    .description = (error_vtable_description_t) error_ok_if_description,
    .free = (error_vtable_free_t) error_ok_if_free,
};

error_t *error_ok_if(bool success, char const *expr) {
    if (success) return NULL;

    error_ok_if_t *result = malloc(sizeof(error_ok_if_t));
    if (result == NULL) return &sentinel_error;

    error_init(&result->error, &error_ok_if_vtable);
    result->expr = expr;

    return (error_t *) result;
}

typedef struct {
    error_t error;
    error_t *primary;
    error_t *secondary;
} error_combine_t;

static error_t const *error_combine_primary(error_combine_t const *self) {
    return self->primary;
}

static error_t const *error_combine_secondary(error_combine_t const *self) {
    return self->secondary;
}

static void error_combine_free(error_combine_t *self) {
    error_free(&self->primary);
    error_free(&self->secondary);
}

static error_vtable_t const error_combine_vtable = {
    .source = (error_vtable_source_t) error_combine_primary,
    .secondary = (error_vtable_source_t) error_combine_secondary,
    .description = (error_vtable_description_t) NULL,
    .free = (error_vtable_free_t) error_combine_free,
};

error_t *error_combine(error_t *primary, error_t *secondary) {
    if (primary == NULL) {
        primary = secondary;
        secondary = NULL;
    }

    if (primary == NULL) {
        return NULL;
    }

    error_combine_t *result = malloc(sizeof(error_combine_t));

    if (result == NULL) {
        error_free(&primary);
        error_free(&secondary);

        return &sentinel_error;
    }

    error_init(&result->error, &error_combine_vtable);
    result->primary = primary;
    result->secondary = secondary;

    return (error_t *) result;
}

error_t *error_wrap(char const *str, error_t *source) {
    if (source == NULL) return NULL;

    return error_from_cstr(str, source);
}

error_t *error_wrap_string(string_t str, error_t *source) {
    if (source == NULL) {
        string_free(&str);

        return NULL;
    }

    return error_from_string(str, source);
}

void error_free(error_t **self) {
    error_t *inner = *self;

    if (inner == NULL) {
        return;
    }

    *self = NULL;

    if (inner == &sentinel_error) {
        return;
    }

    inner->vtable->free(inner);
    backtrace_free(&inner->backtrace);
    free(inner);
}
