#include "common/collections.h"

#pragma GCC diagnostic push

#ifndef VEC_ELEMENT_TYPE
#error "VEC_ELEMENT_TYPE is not defined"
#endif

#ifndef VEC_LABEL
#error "VEC_LABEL is not defined"
#endif

#ifndef VEC_CONFIG
#define VEC_CONFIG COLLECTION_DEFAULT
#endif

#ifndef VEC_GENERIC_NAME
#define VEC_GENERIC_NAME(LABEL, ITEM) \
    CONCAT(vec_, CONCAT(LABEL, CONCAT(_, ITEM)))
#endif

#define VEC_NAME(ITEM) VEC_GENERIC_NAME(VEC_LABEL, ITEM)
#define VEC_TYPE VEC_NAME(t)

#define VEC_ENLARGEMENT_MULTIPLIER 2

#if (VEC_CONFIG) & COLLECTION_STATIC
#define VEC_STATIC static
#pragma GCC diagnostic ignored "-Wunused-function"
#else
#define VEC_STATIC
#endif

#if (VEC_CONFIG) & COLLECTION_DECLARE

#include <stddef.h>

#include "common/error-codes/error-codes.h"

typedef struct {
    VEC_ELEMENT_TYPE *storage;
    size_t capacity;
    size_t len;
} VEC_TYPE;

typedef void (*const VEC_NAME(each_callback_t))(VEC_ELEMENT_TYPE *);

VEC_STATIC VEC_TYPE VEC_NAME(new)(void);
VEC_STATIC VEC_TYPE VEC_NAME(from_raw)(VEC_ELEMENT_TYPE *buf, size_t capacity, size_t len);
VEC_STATIC common_error_code_t VEC_NAME(clone)(VEC_TYPE const *self, VEC_TYPE *result);
VEC_STATIC void VEC_NAME(free)(VEC_TYPE *self);
VEC_STATIC common_error_code_t VEC_NAME(resize)(VEC_TYPE *self, size_t new_capacity);
VEC_STATIC common_error_code_t VEC_NAME(insert)(VEC_TYPE *self, size_t pos, VEC_ELEMENT_TYPE value);
VEC_STATIC common_error_code_t VEC_NAME(push)(VEC_TYPE *self, VEC_ELEMENT_TYPE value);
VEC_STATIC void VEC_NAME(remove)(VEC_TYPE *self, size_t pos);
VEC_STATIC void VEC_NAME(set_len)(VEC_TYPE *self, size_t new_len);
VEC_STATIC void VEC_NAME(clear)(VEC_TYPE *self);
VEC_STATIC VEC_ELEMENT_TYPE const *VEC_NAME(get)(VEC_TYPE const *self, size_t pos);
VEC_STATIC VEC_ELEMENT_TYPE *VEC_NAME(get_mut)(VEC_TYPE *self, size_t pos);
VEC_STATIC VEC_ELEMENT_TYPE const *VEC_NAME(as_ptr)(VEC_TYPE const *self);
VEC_STATIC VEC_ELEMENT_TYPE *VEC_NAME(as_ptr_mut)(VEC_TYPE *self);
VEC_STATIC size_t VEC_NAME(len)(VEC_TYPE const *self);
VEC_STATIC size_t VEC_NAME(capacity)(VEC_TYPE const *self);
VEC_STATIC void VEC_NAME(for_each)(VEC_TYPE *self, VEC_NAME(each_callback_t) callback);

#endif // #if (VEC_CONFIG) & COLLECTION_DECLARE

#if (VEC_CONFIG) & COLLECTION_DEFINE

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "common/error-codes/macros.h"

VEC_STATIC VEC_TYPE VEC_NAME(new)(void) {
    VEC_TYPE result = {
        .storage = NULL,
        .capacity = 0,
        .len = 0,
    };

    return result;
}

VEC_STATIC VEC_TYPE VEC_NAME(from_raw)(VEC_ELEMENT_TYPE *buf, size_t capacity, size_t len) {
    if (capacity == 0) {
        assert(buf == NULL);
    } else {
        assert(buf != NULL);
    }

    assert(len <= capacity);

    return (VEC_TYPE) {
        .storage = buf,
        .capacity = capacity,
        .len = len,
    };
}

VEC_STATIC common_error_code_t VEC_NAME(clone)(VEC_TYPE const *self, VEC_TYPE *result) {
    common_error_code_t code = COMMON_ERROR_CODE_OK;

    *result = VEC_NAME(new)();
    GOTO_ON_ERROR(code = VEC_NAME(resize)(result, self->len), fail);

    memcpy(result->storage, self->storage, self->len * sizeof(VEC_ELEMENT_TYPE));
    result->len = self->len;

    return COMMON_ERROR_CODE_OK;

fail:
    VEC_NAME(free)(result);

    return code;
}

VEC_STATIC void VEC_NAME(free)(VEC_TYPE *self) {
    assert(self != NULL);

    if (self->storage != NULL) {
        free(self->storage);
        self->storage = NULL;
        self->capacity = 0;
        self->len = 0;
    }
}

VEC_STATIC common_error_code_t VEC_NAME(resize)(VEC_TYPE *self, size_t new_capacity) {
    assert(self != NULL);
    assert(new_capacity >= self->len);

    if (new_capacity == 0) {
        VEC_NAME(free)(self);

        return COMMON_ERROR_CODE_OK;
    }

    if (self->capacity == new_capacity) {
        return COMMON_ERROR_CODE_OK;
    }

    VEC_ELEMENT_TYPE *storage = realloc(self->storage, new_capacity * sizeof(VEC_ELEMENT_TYPE));

    if (storage == NULL) {
        return COMMON_ERROR_CODE_MEMORY_ALLOCATION_FAILURE;
    }

    self->storage = storage;
    self->capacity = new_capacity;

    return COMMON_ERROR_CODE_OK;
}

static common_error_code_t VEC_NAME(enlarge)(VEC_TYPE *self) {
    size_t new_capacity = self->capacity * VEC_ENLARGEMENT_MULTIPLIER;

    if (self->capacity == 0) {
        new_capacity = 1;
    }

    return VEC_NAME(resize)(self, new_capacity);
}

VEC_STATIC common_error_code_t
VEC_NAME(insert)(VEC_TYPE *self, size_t pos, VEC_ELEMENT_TYPE value) {
    assert(self != NULL);
    assert(pos <= self->len);

    common_error_code_t code = COMMON_ERROR_CODE_OK;

    if (self->capacity <= self->len) {
        GOTO_ON_ERROR(code = VEC_NAME(enlarge)(self), enlarge_fail);
    }

    size_t elements_to_move = self->len - pos;
    memmove(
        self->storage + pos + 1,
        self->storage + pos,
        elements_to_move * sizeof(VEC_ELEMENT_TYPE)
    );
    self->storage[pos] = value;
    self->len++;

enlarge_fail:
    return code;
}

VEC_STATIC common_error_code_t VEC_NAME(push)(VEC_TYPE *self, VEC_ELEMENT_TYPE value) {
    return VEC_NAME(insert)(self, self->len, value);
}

VEC_STATIC void VEC_NAME(remove)(VEC_TYPE *self, size_t pos) {
    assert(self != NULL);
    assert(pos < self->len);

    size_t elements_to_move = self->len - pos - 1;
    memmove(
        self->storage + pos,
        self->storage + pos + 1,
        elements_to_move * sizeof(VEC_ELEMENT_TYPE)
    );
    self->len--;
}

VEC_STATIC void VEC_NAME(set_len)(VEC_TYPE *self, size_t new_len) {
    assert(self != NULL);
    assert(new_len <= self->capacity);

    self->len = new_len;
}

VEC_STATIC void VEC_NAME(clear)(VEC_TYPE *self) {
    assert(self != NULL);

    VEC_NAME(set_len)(self, 0);
}

VEC_STATIC VEC_ELEMENT_TYPE const *VEC_NAME(get)(VEC_TYPE const *self, size_t pos) {
    assert(self != NULL);

    if (pos >= self->len) {
        return NULL;
    }

    return &self->storage[pos];
}

VEC_STATIC VEC_ELEMENT_TYPE *VEC_NAME(get_mut)(VEC_TYPE *self, size_t pos) {
    assert(self != NULL);

    if (pos >= self->len) {
        return NULL;
    }

    return &self->storage[pos];
}

VEC_STATIC VEC_ELEMENT_TYPE const *VEC_NAME(as_ptr)(VEC_TYPE const *self) {
    assert(self != NULL);

    if (self->len == 0) {
        return NULL;
    }

    return self->storage;
}

VEC_STATIC VEC_ELEMENT_TYPE *VEC_NAME(as_ptr_mut)(VEC_TYPE *self) {
    assert(self != NULL);

    if (self->len == 0) {
        return NULL;
    }

    return self->storage;
}

VEC_STATIC size_t VEC_NAME(len)(VEC_TYPE const *self) {
    assert(self != NULL);

    return self->len;
}

VEC_STATIC size_t VEC_NAME(capacity)(VEC_TYPE const *self) {
    assert(self != NULL);

    return self->capacity;
}

VEC_STATIC void VEC_NAME(for_each)(VEC_TYPE *self, VEC_NAME(each_callback_t) callback) {
    assert(self != NULL);
    assert(callback != NULL);

    for (size_t i = 0; i < self->len; ++i) {
        callback(VEC_NAME(get_mut)(self, i));
    }
}

#endif // #if (VEC_CONFIG) & COLLECTION_DEFINE

#undef VEC_STATIC

#undef VEC_ENLARGEMENT_MULTIPLIER
#undef VEC_TYPE
#undef VEC_NAME

#if !((VEC_CONFIG) & COLLECTION_EXPORT_GENERIC_NAME)
#undef VEC_GENERIC_NAME
#endif

#undef VEC_CONFIG
#undef VEC_LABEL
#undef VEC_ELEMENT_TYPE

#pragma GCC diagnostic pop
