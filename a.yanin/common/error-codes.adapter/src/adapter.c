#include "common/error-codes/adapter.h"

#include <stdlib.h>

#include <common/error-codes/display.h>

typedef struct {
    error_t error;
    common_error_code_t code;
} error_common_t;

static void error_common_description(error_common_t const *self, string_t *buf) {
    string_appendf(buf, "%s", common_error_code_as_str(self->code));
}

static void error_common_free(error_common_t *) {}

static error_vtable_t const error_common_vtable = {
    .source = error_null,
    .secondary = error_null,
    .description = (error_vtable_description_t) error_common_description,
    .free = (error_vtable_free_t) error_common_free,
};

error_t *error_from_common(common_error_code_t code) {
    if (code == COMMON_ERROR_CODE_OK) return NULL;

    error_common_t *result = malloc(sizeof(error_common_t));

    if (result == NULL) {
        return &sentinel_error;
    }

    error_init(&result->error, &error_common_vtable);
    result->code = code;

    return (error_t *) result;
}
