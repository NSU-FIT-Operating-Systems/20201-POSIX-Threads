#pragma once

#include <stdbool.h>

#include "common/error-codes/error-codes.h"

#define VEC_ELEMENT_TYPE unsigned char
#define VEC_LABEL uchar
#define VEC_CONFIG COLLECTION_DECLARE
#include "common/collections/vec.h"

typedef struct {
    vec_uchar_t storage;
} string_t;

common_error_code_t string_new(string_t *result);
common_error_code_t string_from_cstr(char const *cstr, string_t *result);
common_error_code_t string_from_slice(char const *begin, size_t count, string_t *result);
void string_from_raw(char *buf, size_t capacity, string_t *result);

[[gnu::format(printf, 2, 3)]]
common_error_code_t string_sprintf(string_t *result, char const *format, ...);
[[gnu::format(printf, 2, 3)]]
common_error_code_t string_appendf(string_t *self, char const *format, ...);

void string_free(string_t *self);
common_error_code_t string_insert(string_t *self, size_t pos, unsigned char ch);
common_error_code_t string_push(string_t *self, unsigned char ch);
common_error_code_t string_append(string_t *self, string_t const *other);
common_error_code_t string_append_slice(string_t *self, char const *begin, size_t count);
void string_remove(string_t *self, size_t pos);
void string_pop(string_t *self);
void string_clear(string_t *self);
unsigned char string_get(string_t const *self, size_t pos);
unsigned char const *string_as_ptr(string_t const *self);
char const *string_as_cptr(string_t const *self);
bool string_equals(string_t const *self, string_t const *other);
int string_cmp(string_t const *self, string_t const *other);
size_t string_len(string_t const *self);
size_t string_capacity(string_t const *self);
