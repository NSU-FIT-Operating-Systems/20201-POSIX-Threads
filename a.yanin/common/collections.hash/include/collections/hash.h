#include "common/collections.h"

#pragma GCC diagnostic push

#ifndef HASH_KEY_TYPE
#error "HASH_KEY_TYPE is not defined"
#endif

#ifndef HASH_VALUE_TYPE
#error "HASH_VALUE_TYPE is not defined"
#endif

#ifndef HASH_LABEL
#error "HASH_LABEL is not defined"
#endif

#ifndef HASH_CONFIG
#define HASH_CONFIG COLLECTION_DEFAULT
#endif

#ifndef HASH_GENERIC_NAME
#define HASH_GENERIC_NAME(LABEL, ITEM) \
    CONCAT(hash_, CONCAT(LABEL, CONCAT(_, ITEM)))
#endif // HASH_GENERIC_NAME

#define HASH_NAME(ITEM) HASH_GENERIC_NAME(HASH_LABEL, ITEM)

#define HASH_TYPE HASH_NAME(t)
#define HASH_ELEMENT_TYPE HASH_NAME(key_value_t)

#define HASH_ENLARGEMENT_MULTIPLIER 2
#define HASH_MAX_LOAD_FACTOR 0.5
#define HASH_MIN_CAPACITY 8

#if (HASH_CONFIG) & COLLECTION_STATIC
#define HASH_STATIC static
#pragma GCC diagnostic ignored "-Wunused-function"
#else
#define HASH_STATIC
#endif

#if (HASH_CONFIG) & COLLECTION_DECLARE

#include <stddef.h>

#include "common/error-codes/error-codes.h"

typedef enum {
    HASH_NAME(STATE_FREE),
    HASH_NAME(STATE_DELETED),
    HASH_NAME(STATE_OCCUPIED),
} HASH_NAME(element_state_t);

typedef struct {
    HASH_NAME(element_state_t) state;
    HASH_KEY_TYPE key;
    HASH_VALUE_TYPE value;
} HASH_ELEMENT_TYPE;

typedef size_t (*HASH_NAME(hasher_t))(HASH_KEY_TYPE const *, void *);
typedef bool (*HASH_NAME(eq_t))(HASH_KEY_TYPE const *, HASH_KEY_TYPE const *);
typedef bool (*HASH_NAME(callback_t))(HASH_KEY_TYPE const *, HASH_VALUE_TYPE const *, void *);

typedef struct {
    HASH_NAME(hasher_t) hasher;
    void *opaque_data;
} HASH_NAME(hasher_data_t);

typedef struct {
    HASH_ELEMENT_TYPE *storage;
    size_t capacity;
    size_t len;
    size_t non_free_entries;
    bool rehashing;

    HASH_NAME(hasher_data_t) primary;
    HASH_NAME(hasher_data_t) secondary;
    HASH_NAME(eq_t) eq;
} HASH_TYPE;

HASH_STATIC common_error_code_t HASH_NAME(new)(
    HASH_NAME(hasher_data_t) primary_hasher,
    HASH_NAME(hasher_data_t) secondary_hasher,
    HASH_NAME(eq_t) eq_comparator,
    HASH_TYPE *result
);
HASH_STATIC void HASH_NAME(free)(HASH_TYPE *self);
HASH_STATIC common_error_code_t HASH_NAME(insert)(
    HASH_TYPE *self,
    HASH_KEY_TYPE key,
    HASH_VALUE_TYPE value
);
HASH_STATIC common_error_code_t HASH_NAME(remove)(
    HASH_TYPE *self,
    HASH_KEY_TYPE const *key,
    HASH_KEY_TYPE *stored_key_result,
    HASH_VALUE_TYPE *stored_value_result
);
HASH_STATIC HASH_VALUE_TYPE const *HASH_NAME(get)(HASH_TYPE const *self, HASH_KEY_TYPE const *key);

// Iterates over the contents of the hash table, calling the callback for each
// key-value pair.
//
// The callback should return false to continue iteration and true to stop it.
HASH_STATIC void HASH_NAME(for_each)(
    HASH_TYPE const *self,
    HASH_NAME(callback_t) callback,
    void *opaque_data
);

HASH_STATIC size_t HASH_NAME(len)(HASH_TYPE const *self);
HASH_STATIC size_t HASH_NAME(capacity)(HASH_TYPE const *self);

#endif // #if (HASH_CONFIG) & COLLECTION_DECLARE

#if (HASH_CONFIG) & COLLECTION_DEFINE

#include <assert.h>
#include <stdlib.h>

static common_error_code_t HASH_NAME(rehash)(HASH_TYPE *self) {
    assert(self != NULL);
    assert(!self->rehashing);

    size_t new_capacity = self->capacity * HASH_ENLARGEMENT_MULTIPLIER;

    if (new_capacity == 0) {
        new_capacity = HASH_MIN_CAPACITY;
    }

    HASH_ELEMENT_TYPE *storage = calloc(new_capacity, sizeof(HASH_ELEMENT_TYPE));

    if (storage == NULL) {
        return COMMON_ERROR_CODE_MEMORY_ALLOCATION_FAILURE;
    }

    for (size_t i = 0; i < new_capacity; ++i) {
        storage[i].state = HASH_NAME(STATE_FREE);
    }

    HASH_TYPE new_map = {
        .storage = storage,
        .capacity = new_capacity,
        .len = 0,
        .non_free_entries = 0,
        .rehashing = true,
        .primary = self->primary,
        .secondary = self->secondary,
        .eq = self->eq,
    };

    for (size_t i = 0; i < self->capacity; ++i) {
        if (self->storage[i].state == HASH_NAME(STATE_OCCUPIED)) {
            HASH_NAME(insert)(&new_map, self->storage[i].key, self->storage[i].value);
        }
    }

    free(self->storage);
    *self = new_map;
    self->rehashing = false;

    return COMMON_ERROR_CODE_OK;
}

HASH_STATIC common_error_code_t HASH_NAME(new)(
    HASH_NAME(hasher_data_t) primary_hasher,
    HASH_NAME(hasher_data_t) secondary_hasher,
    HASH_NAME(eq_t) eq_comparator,
    HASH_TYPE *result
) {
    assert(primary_hasher.hasher != NULL);
    assert(secondary_hasher.hasher != NULL);
    assert(eq_comparator != NULL);
    assert(result != NULL);

    result->storage = NULL;
    result->capacity = 0;
    result->len = 0;
    result->non_free_entries = 0;
    result->rehashing = false;
    result->primary = primary_hasher;
    result->secondary = secondary_hasher;
    result->eq = eq_comparator;

    TRY(HASH_NAME(rehash)(result));

    return COMMON_ERROR_CODE_OK;
}

HASH_STATIC void HASH_NAME(free)(HASH_TYPE *self) {
    assert(self != NULL);

    free(self->storage);
    self->storage = NULL;
    self->len = 0;
    self->capacity = 0;
}

static bool should_rehash(size_t non_free_entries, size_t capacity) {
    assert(capacity > 0);

    return (double) non_free_entries / capacity >= HASH_MAX_LOAD_FACTOR;
}

static size_t HASH_NAME(index_for_key)(
    HASH_TYPE const *self,
    HASH_KEY_TYPE const *key,
    bool skip_deleted
) {
    assert(self != NULL);
    assert(key != NULL);

    size_t index = self->primary.hasher(key, self->primary.opaque_data) % self->capacity;
    size_t offset = 0;

    while (true) {
        HASH_ELEMENT_TYPE const *elem = &self->storage[index];

        if (elem->state == HASH_NAME(STATE_DELETED) && !skip_deleted) {
            break;
        }

        if (elem->state == HASH_NAME(STATE_FREE)) {
            break;
        }

        if (elem->state == HASH_NAME(STATE_OCCUPIED) &&
                self->eq(key, &elem->key)) {
            break;
        }

        if (offset == 0) {
            offset = self->secondary.hasher(key, self->secondary.opaque_data);
            offset %= self->capacity;

            if (offset == 0) {
                offset = 1;
            }
        }

        index = (index + offset) % self->capacity;
    }

    return index;
}

HASH_STATIC common_error_code_t HASH_NAME(insert)(
    HASH_TYPE *self,
    HASH_KEY_TYPE key,
    HASH_VALUE_TYPE value
) {
    assert(self != NULL);

    if (should_rehash(self->non_free_entries, self->capacity)) {
        TRY(HASH_NAME(rehash)(self));
    }

    size_t pos = HASH_NAME(index_for_key)(self, &key, false);
    self->storage[pos].key = key;
    self->storage[pos].value = value;
    self->storage[pos].state = HASH_NAME(STATE_OCCUPIED);
    ++self->len;
    ++self->non_free_entries;

    return COMMON_ERROR_CODE_OK;
}

HASH_STATIC common_error_code_t HASH_NAME(remove)(
    HASH_TYPE *self,
    HASH_KEY_TYPE const *key,
    HASH_KEY_TYPE *stored_key_result,
    HASH_VALUE_TYPE *stored_value_result
) {
    assert(self != NULL);
    assert(key != NULL);

    size_t pos = HASH_NAME(index_for_key)(self, key, true);

    if (self->storage[pos].state != HASH_NAME(STATE_OCCUPIED)) {
        return COMMON_ERROR_CODE_NOT_FOUND;
    }

    self->storage[pos].state = HASH_NAME(STATE_DELETED);
    --self->len;

    if (stored_key_result != NULL) {
        *stored_key_result = self->storage[pos].key;
    }

    if (stored_value_result != NULL) {
        *stored_value_result = self->storage[pos].value;
    }

    return COMMON_ERROR_CODE_OK;
}

HASH_STATIC HASH_VALUE_TYPE const *HASH_NAME(get)(HASH_TYPE const *self, HASH_KEY_TYPE const *key) {
    assert(self != NULL);
    assert(key != NULL);

    size_t pos = HASH_NAME(index_for_key)(self, key, true);

    if (self->storage[pos].state != HASH_NAME(STATE_OCCUPIED)) {
        return NULL;
    }

    return &self->storage[pos].value;
}

HASH_STATIC void HASH_NAME(for_each)(
    HASH_TYPE const *self,
    HASH_NAME(callback_t) callback,
    void *opaque_data
) {
    assert(self != NULL);
    assert(callback != NULL);

    for (size_t i = 0; i < self->capacity; ++i) {
        HASH_ELEMENT_TYPE const *element = &self->storage[i];

        if (element->state == HASH_NAME(STATE_OCCUPIED)) {
            if (callback(&element->key, &element->value, opaque_data)) {
                break;
            }
        }
    }
}

HASH_STATIC size_t HASH_NAME(len)(HASH_TYPE const *self) {
    assert(self != NULL);

    return self->len;
}

HASH_STATIC size_t HASH_NAME(capacity)(HASH_TYPE const *self) {
    assert(self != NULL);

    return self->capacity;
}

#endif // #if (HASH_CONFIG) & COLLECTION_DEFINE

#undef HASH_STATIC

#undef HASH_ENLARGEMENT_MULTIPLIER
#undef HASH_MIN_CAPACITY
#undef HASH_MAX_LOAD_FACTOR
#undef HASH_VEC_LABEL
#undef HASH_ELEMENT_TYPE
#undef HASH_TYPE
#undef HASH_NAME

#if !((HASH_CONFIG) & COLLECTION_EXPORT_GENERIC_NAME)
#undef HASH_GENERIC_NAME
#endif

#undef HASH_CONFIG
#undef HASH_LABEL
#undef HASH_VALUE_TYPE
#undef HASH_KEY_TYPE

#pragma GCC diagnostic pop
