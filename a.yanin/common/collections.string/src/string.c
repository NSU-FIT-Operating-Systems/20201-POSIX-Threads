#include <common/collections/string.h>

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define VEC_ELEMENT_TYPE unsigned char
#define VEC_LABEL uchar
#define VEC_CONFIG COLLECTION_DEFINE
#include <common/collections/vec.h>

common_error_code_t string_new(string_t *result) {
    assert(result != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    string_t instance = {
        .storage = vec_uchar_new(),
    };

    GOTO_ON_ERROR(status = vec_uchar_push(&instance.storage, '\0'), fail);

    *result = instance;

    return COMMON_ERROR_CODE_OK;

fail:
    vec_uchar_free(&instance.storage);

    return status;
}

common_error_code_t string_from_cstr(char const *cstr, string_t *result) {
    assert(cstr != NULL);
    assert(result != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    GOTO_ON_ERROR(status = string_new(result), string_new_fail);
    char ch;

    while ((ch = *(cstr++)) != '\0') {
        GOTO_ON_ERROR(status = string_push(result, ch), string_push_fail);
    }

    return COMMON_ERROR_CODE_OK;

string_push_fail:
    string_free(result);

string_new_fail:
    return status;
}

common_error_code_t string_from_slice(char const *begin, size_t count, string_t *result) {
    assert(count == 0 || begin != NULL);
    assert(result != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    GOTO_ON_ERROR(status = string_new(result), string_new_fail);

    for (size_t i = 0; i < count; ++i) {
        GOTO_ON_ERROR(status = string_push(result, begin[i]), string_push_fail);
    }

    return COMMON_ERROR_CODE_OK;

string_push_fail:
    string_free(result);

string_new_fail:
    return status;
}

common_error_code_t string_sprintf(string_t *result, const char *format, ...) {
    assert(result != NULL);
    assert(format != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    va_list args;
    va_start(args, format);

    va_list args_copy;
    va_copy(args_copy, args);

    int length = vsnprintf(NULL, 0, format, args);

    string_t instance = {
        .storage = vec_uchar_new(),
    };

    GOTO_ON_ERROR(status = vec_uchar_resize(&instance.storage, length + 1), fail);
    vec_uchar_set_len(&instance.storage, length + 1);

    vsnprintf((char *) vec_uchar_as_ptr_mut(&instance.storage), length + 1, format, args_copy);

    *result = instance;

fail:
    return status;
}

common_error_code_t string_appendf(string_t *self, char const *format, ...) {
    assert(self != NULL);
    assert(format != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    va_list args;
    va_start(args, format);

    va_list args_copy;
    va_copy(args_copy, args);

    int length = vsnprintf(NULL, 0, format, args);
    size_t start = vec_uchar_len(&self->storage);

    if (start > 0) {
        // start writing at the position of the NUL terminator
        --start;
    }

    size_t new_size = start + length + 1;

    GOTO_ON_ERROR(status = vec_uchar_resize(&self->storage, new_size), fail);
    vec_uchar_set_len(&self->storage, new_size);

    vsnprintf((char *) vec_uchar_as_ptr_mut(&self->storage) + start, length + 1, format, args_copy);

fail:
    return status;
}

void string_from_raw(char *buf, size_t capacity, string_t *result) {
    assert(buf != NULL);
    assert(capacity > 0);
    assert(result != NULL);

    size_t length = strlen(buf);
    assert(length < capacity);

    *result = (string_t) {
        .storage = vec_uchar_from_raw((unsigned char *) buf, capacity, length + 1),
    };
}

common_error_code_t string_clone(string_t const *self, string_t *result) {
    assert(self != NULL);
    assert(result != NULL);

    return vec_uchar_clone(&self->storage, &result->storage);
}

void string_free(string_t *self) {
    assert(self != NULL);

    vec_uchar_free(&self->storage);
}

common_error_code_t string_insert(string_t *self, size_t pos, unsigned char ch) {
    assert(self != NULL);

    // make sure we aren't writing past our NUL terminator...
    assert(pos < vec_uchar_len(&self->storage));

    // ...and the rest is handled by the vector
    common_error_code_t status = vec_uchar_insert(&self->storage, pos, ch);

    return status;
}

common_error_code_t string_push(string_t *self, unsigned char ch) {
    assert(self != NULL);

    return string_insert(self, vec_uchar_len(&self->storage) - 1, ch);
}

common_error_code_t string_append(string_t *self, string_t const *other) {
    assert(self != NULL);
    assert(other != NULL);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    vec_uchar_remove(&self->storage, vec_uchar_len(&self->storage) - 1);
    GOTO_ON_ERROR(status = vec_uchar_append(&self->storage, &other->storage), fail);

    return status;

fail:
    vec_uchar_push(&self->storage, '\0');

    return status;
}

common_error_code_t string_append_slice(string_t *self, char const *begin, size_t count) {
    assert(self != NULL);
    assert(begin != NULL || count == 0);

    common_error_code_t status = COMMON_ERROR_CODE_OK;

    size_t len = vec_uchar_len(&self->storage);
    vec_uchar_remove(&self->storage, len - 1);
    status = vec_uchar_append_slice(&self->storage, (unsigned char const *) begin, count);

    common_error_code_t null_status = vec_uchar_push(&self->storage, '\0');

    if (status == COMMON_ERROR_CODE_OK) {
        status = null_status;
    }

    if (status != COMMON_ERROR_CODE_OK) {
        // the `len - 1`th element is always available:
        // - if append failed, the `null_status` must have succeeded because no reallocation has
        //   occured
        // - if append succeeded but the string could not be terminated, reallocation has occured
        //   and the vector's length has grown
        *vec_uchar_get_mut(&self->storage, len - 1) = '\0';
        vec_uchar_set_len(&self->storage, len);
    }

    return status;
}

void string_remove(string_t *self, size_t pos) {
    assert(self != NULL);
    assert(pos < string_len(self));

    vec_uchar_remove(&self->storage, pos);
}

void string_pop(string_t *self) {
    assert(self != NULL);
    assert(string_len(self) > 0);

    string_remove(self, string_len(self) - 1);
}

void string_clear(string_t *self) {
    assert(self != NULL);

    // the string always has at least 1 element
    vec_uchar_set_len(&self->storage, 1);
    *vec_uchar_get_mut(&self->storage, 0) = '\0';
}

void string_remove_slice(string_t *self, size_t start, size_t end) {
    assert(self != NULL);
    assert(start <= end);

    if (end > string_len(self)) {
        end = string_len(self);
    }

    vec_uchar_remove_slice(&self->storage, start, end);
}

unsigned char string_get(string_t const *self, size_t pos) {
    assert(self != NULL);

    return *vec_uchar_get(&self->storage, pos);
}

unsigned char *string_get_mut(string_t *self, size_t pos) {
    assert(self != NULL);
    assert(pos < string_len(self));

    return vec_uchar_get_mut(&self->storage, pos);
}

unsigned char const *string_as_ptr(string_t const *self) {
    assert(self != NULL);

    return vec_uchar_as_ptr(&self->storage);
}

char const *string_as_cptr(string_t const *self) {
    assert(self != NULL);

    return (char *) string_as_ptr(self);
}

char *string_as_cptr_mut(string_t *self) {
    assert(self != NULL);

    return (char *) vec_uchar_as_ptr_mut(&self->storage);
}

bool string_equals(string_t const *self, string_t const *other) {
    assert(self != NULL);
    assert(other != NULL);

    if (string_len(self) != string_len(other)) {
        return false;
    }

    return string_cmp(self, other) == 0;
}

int string_cmp(string_t const *self, string_t const *other) {
    assert(self != NULL);
    assert(other != NULL);

    size_t len = string_len(self);
    size_t other_len = string_len(other);

    if (other_len < len) {
        len = other_len;
    }

    return memcmp(
        (void const *) string_as_ptr(self),
        (void const *) string_as_ptr(other),
        // safe: string_as_ptr(s)[string_len(s)] == '\0'
        len + 1
    );
}

size_t string_len(string_t const *self) {
    assert(self != NULL);

    return vec_uchar_len(&self->storage) - 1;
}

size_t string_capacity(string_t const *self) {
    assert(self != NULL);

    return vec_uchar_capacity(&self->storage);
}

void string_set_len(string_t *self, size_t len) {
    assert(self != NULL);
    assert(len <= string_len(self));

    vec_uchar_set_len(&self->storage, len + 1);
    *vec_uchar_get_mut(&self->storage, len) = '\0';
}
