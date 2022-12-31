#include "common/posix/adapter.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    error_t error;
    posix_err_t err;
} error_posix_t;

static void error_posix_description(error_posix_t const *self, string_t *buf) {
    string_appendf(buf, "%s [error code %d: %s]",
        self->err.message,
        self->err.errno_code,
        strerror(self->err.errno_code));
}

static void error_posix_free(error_posix_t *) {}

static error_vtable_t const error_posix_vtable = {
    .source = error_null,
    .secondary = error_null,
    .description = (error_vtable_description_t) error_posix_description,
    .free = (error_vtable_free_t) error_posix_free,
};

error_t *error_from_posix(posix_err_t err) {
    if (err.errno_code == 0) return NULL;

    error_posix_t *result = malloc(sizeof(error_posix_t));

    if (result == NULL) {
        return &sentinel_error;
    }

    error_init(&result->error, &error_posix_vtable);
    result->err = err;

    return (error_t *) result;
}
