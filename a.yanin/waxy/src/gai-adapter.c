#include "gai-adapter.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    error_t error;
    int gai_code;
    int errno_code;
} error_from_gai_t;

static void error_from_gai_description(error_from_gai_t const *self, string_t *buf) {
    if (self->gai_code == EAI_SYSTEM) {
        string_appendf(buf, "%s", strerror(self->errno_code));
    } else {
        string_appendf(buf, "%s", gai_strerror(self->gai_code));
    }
}

static void error_from_gai_free(error_from_gai_t *) {}

static error_vtable_t const error_from_gai_vtable = {
    .source = error_null,
    .secondary = error_null,
    .description = (error_vtable_description_t) error_from_gai_description,
    .free = (error_vtable_free_t) error_from_gai_free,
};

error_t *error_from_gai(int code) {
    if (code == 0) return NULL;

    int errno_code = errno;

    error_from_gai_t *result = malloc(sizeof(error_from_gai_t));
    if (result == NULL) return &sentinel_error;

    error_init(&result->error, &error_from_gai_vtable);
    result->gai_code = code;
    result->errno_code = errno_code;

    return (error_t *) result;
}
