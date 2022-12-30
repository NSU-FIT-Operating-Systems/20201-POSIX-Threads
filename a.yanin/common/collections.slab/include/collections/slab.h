#include "common/collections.h"

#pragma GCC diagnostic push

#ifndef SLAB_ELEMENT_TYPE
#error "SLAB_ELEMENT_TYPE is not defined"
#endif

#ifndef SLAB_LABEL
#error "SLAB_LABEL is not defined"
#endif

#ifndef SLAB_CONFIG
#define SLAB_CONFIG COLLECTION_DEFAULT
#endif

#ifndef SLAB_GENERIC_NAME
#define SLAB_GENERIC_NAME(LABEL, ITEM) \
    CONCAT(slab_, CONCAT(LABEL, CONCAT(_, ITEM)))
#endif

#define SLAB_NAME(ITEM) SLAB_GENERIC_NAME(SLAB_LABEL, ITEM)
#define SLAB_TYPE SLAB_NAME(t)

#define SLAB_NODE_TYPE SLAB_NAME(node_t)
#define SLAB_VEC_NAME(ITEM) VEC_GENERIC_NAME(SLAB_NAME(node), ITEM)

#if (SLAB_CONFIG) & COLLECTION_STATIC
#define SLAB_STATIC static
#pragma GCC diagnostic ignored "-Wunused-function"
#else
#define SLAB_STATIC
#endif

#if (SLAB_CONFIG) & COLLECTION_DECLARE

#include <stddef.h>
#include <stdbool.h>

#include "common/error-codes/error-codes.h"

typedef struct {
    SLAB_ELEMENT_TYPE value;

    bool occupied;
    size_t next;
    size_t prev;
} SLAB_NODE_TYPE;

#define VEC_ELEMENT_TYPE SLAB_NODE_TYPE
#define VEC_LABEL SLAB_NAME(node)
#define VEC_CONFIG \
    (COLLECTION_DECLARE \
    | COLLECTION_EXPORT_GENERIC_NAME \
    | ((SLAB_CONFIG) & COLLECTION_STATIC))
#include "common/collections/vec.h"

typedef struct {
    SLAB_VEC_NAME(t) storage;
    size_t capacity;
    size_t len;
    size_t max_index;

    size_t head;
    size_t end;
} SLAB_TYPE;

typedef struct SLAB_NAME(iter) {
    SLAB_TYPE const *slab;
    size_t current;

    bool (*next)(struct SLAB_NAME(iter) *, size_t *);
} SLAB_NAME(iter_t);

SLAB_STATIC SLAB_TYPE SLAB_NAME(new)(void);
SLAB_STATIC SLAB_TYPE SLAB_NAME(new_bounded)(size_t max_index);
SLAB_STATIC void SLAB_NAME(free)(SLAB_TYPE *self);

SLAB_STATIC common_error_code_t SLAB_NAME(append)(SLAB_TYPE *self, SLAB_ELEMENT_TYPE value,
    size_t *result_index);
SLAB_STATIC common_error_code_t SLAB_NAME(add_after)(SLAB_TYPE *self, size_t prev_index,
    SLAB_ELEMENT_TYPE value, size_t *result_index);
SLAB_STATIC void SLAB_NAME(move_to_end)(SLAB_TYPE *self, size_t index);
SLAB_STATIC void SLAB_NAME(remove)(SLAB_TYPE *self, size_t index);

SLAB_STATIC size_t SLAB_NAME(get_head)(SLAB_TYPE const *self);
SLAB_STATIC size_t SLAB_NAME(get_end)(SLAB_TYPE const *self);

SLAB_STATIC SLAB_ELEMENT_TYPE const *SLAB_NAME(get)(SLAB_TYPE const *self, size_t index);
SLAB_STATIC SLAB_ELEMENT_TYPE *SLAB_NAME(get_mut)(SLAB_TYPE *self, size_t index);

SLAB_STATIC size_t SLAB_NAME(len)(SLAB_TYPE const *self);
SLAB_STATIC size_t SLAB_NAME(capacity)(SLAB_TYPE const *self);

SLAB_STATIC SLAB_NAME(iter_t) SLAB_NAME(iter)(SLAB_TYPE const *self, size_t start);
SLAB_STATIC bool SLAB_NAME(iter_next)(SLAB_NAME(iter_t) *self, size_t *result);

#endif // #if (SLAB_CONFIG) & COLLECTION_DECLARE

#if (SLAB_CONFIG) & COLLECTION_DEFINE

#define VEC_ELEMENT_TYPE SLAB_NODE_TYPE
#define VEC_LABEL SLAB_NAME(node)
#define VEC_CONFIG \
    (COLLECTION_DEFINE \
    | COLLECTION_EXPORT_GENERIC_NAME \
    | ((SLAB_CONFIG) & COLLECTION_STATIC))
#include "common/collections/vec.h"

SLAB_STATIC SLAB_TYPE SLAB_NAME(new)(void) {
    return SLAB_NAME(new_bounded)(SIZE_MAX);
}

SLAB_STATIC SLAB_TYPE SLAB_NAME(new_bounded)(size_t max_index) {
    return (SLAB_TYPE) {
        .storage = SLAB_VEC_NAME(new)(),
        .capacity = 0,
        .len = 0,
        .max_index = max_index,

        .head = 0,
        .end = 0,
    };
}

SLAB_STATIC void SLAB_NAME(free)(SLAB_TYPE *self) {
    assert(self != NULL);

    SLAB_VEC_NAME(free)(&self->storage);
}

static bool SLAB_NAME(check_bounds)(SLAB_TYPE const *self, size_t index) {
    assert(self != NULL);

    return 0 < index && index <= self->capacity &&
        index <= SLAB_VEC_NAME(len)(&self->storage);
}

static SLAB_NODE_TYPE const *SLAB_NAME(get_node)(SLAB_TYPE const *self, size_t index) {
    assert(self != NULL);
    assert(SLAB_NAME(check_bounds)(self, index));

    return SLAB_VEC_NAME(get)(&self->storage, index - 1);
}

static SLAB_NODE_TYPE *SLAB_NAME(get_node_mut)(SLAB_TYPE *self, size_t index) {
    assert(self != NULL);
    assert(SLAB_NAME(check_bounds)(self, index));

    return SLAB_VEC_NAME(get_mut)(&self->storage, index - 1);
}

SLAB_STATIC common_error_code_t SLAB_NAME(append)(
    SLAB_TYPE *self,
    SLAB_ELEMENT_TYPE value,
    size_t *result_index
) {
    assert(self != NULL);

    common_error_code_t code = COMMON_ERROR_CODE_OK;

    size_t node_index;

    if (SLAB_VEC_NAME(len)(&self->storage) < self->capacity || self->capacity <= self->len) {
        if (self->max_index != SIZE_MAX &&
                SLAB_VEC_NAME(len)(&self->storage) == self->max_index + 1) {
            return COMMON_ERROR_CODE_OVERFLOW;
        }

        GOTO_ON_ERROR(code = SLAB_VEC_NAME(push)(
            &self->storage,
            (SLAB_NODE_TYPE) {
                .value = value,

                .occupied = true,
                .prev = self->end,
                .next = 0,
            }
        ), push_fail);

        node_index = SLAB_VEC_NAME(len)(&self->storage);
    } else {
        node_index = SLAB_NAME(get_node)(self, self->end)->next;
        assert(node_index != 0);

        SLAB_NODE_TYPE *node = SLAB_NAME(get_node_mut)(self, node_index);
        assert(!node->occupied);

        node->value = value;
        node->prev = self->end;
        node->occupied = true;
    }

    if (self->capacity <= self->len) {
        // the main if branch above should've resized the backing storage
        self->capacity = SLAB_VEC_NAME(capacity)(&self->storage);
    }

    if (self->end != 0) {
        SLAB_NAME(get_node_mut)(self, self->end)->next = node_index;
    }

    self->end = node_index;
    ++self->len;

    if (self->head == 0) {
        self->head = node_index;
    }

    if (result_index != NULL) {
        *result_index = node_index;
    }

push_fail:
    return code;
}

SLAB_STATIC common_error_code_t SLAB_NAME(add_after)(
    SLAB_TYPE *self,
    size_t prev_index,
    SLAB_ELEMENT_TYPE value,
    size_t *result_index
) {
    assert(self != NULL);
    assert(SLAB_NAME(check_bounds)(self, prev_index));

    common_error_code_t code = COMMON_ERROR_CODE_OK;

    size_t node_index;
    GOTO_ON_ERROR(code = SLAB_NAME(append)(self, value, &node_index), append_fail);

    SLAB_NODE_TYPE *prev = SLAB_NAME(get_node_mut)(self, prev_index);
    SLAB_NODE_TYPE *node = SLAB_NAME(get_node_mut)(self, node_index);

    if (prev_index == node->prev) {
        return ERROR_CODE_OK;
    }

    self->end = node->prev;
    SLAB_NAME(get_node_mut)(self, prev->next)->prev = node_index;
    SLAB_NAME(get_node_mut)(self, node->prev)->next = node->next;
    node->next = prev->next;
    prev->next = node_index;
    node->prev = prev_index;

    if (result_index != NULL) {
        *result_index = node_index;
    }

append_fail:
    return code;
}

SLAB_STATIC void SLAB_NAME(move_to_end)(SLAB_TYPE *self, size_t index) {
    assert(self != NULL);
    assert(SLAB_NAME(check_bounds)(self, index));

    if (self->len <= 1) {
        return;
    }

    if (index == self->end) {
        return;
    }

    SLAB_NODE_TYPE *node = SLAB_NAME(get_node_mut)(self, index);

    if (index == self->head) {
        self->head = SLAB_NAME(get_node)(self, self->head)->next;
    }

    if (node->prev != 0) {
        SLAB_NAME(get_node_mut)(self, node->prev)->next = node->next;
    }

    if (node->next != 0) {
        SLAB_NAME(get_node_mut)(self, node->next)->prev = node->prev;
    }

    node->prev = self->end;
    node->next = SLAB_NAME(get_node)(self, self->end)->next;
    SLAB_NAME(get_node_mut)(self, self->end)->next = index;
    self->end = index;
}

SLAB_STATIC void SLAB_NAME(remove)(SLAB_TYPE *self, size_t index) {
    assert(self != NULL);
    assert(SLAB_NAME(check_bounds)(self, index));

    SLAB_NAME(move_to_end)(self, index);
    SLAB_NODE_TYPE *end_node = SLAB_NAME(get_node_mut)(self, self->end);
    assert(end_node->occupied);
    end_node->occupied = false;

    self->end = end_node->prev;
    --self->len;
}

SLAB_STATIC size_t SLAB_NAME(get_head)(SLAB_TYPE const *self) {
    assert(self != NULL);

    return self->head;
}

SLAB_STATIC size_t SLAB_NAME(get_end)(SLAB_TYPE const *self) {
    assert(self != NULL);

    return self->end;
}

SLAB_STATIC SLAB_ELEMENT_TYPE const *SLAB_NAME(get)(SLAB_TYPE const *self, size_t index) {
    assert(self != NULL);
    assert(SLAB_NAME(check_bounds)(self, index));

    return &SLAB_NAME(get_node)(self, index)->value;
}

SLAB_STATIC SLAB_ELEMENT_TYPE *SLAB_NAME(get_mut)(SLAB_TYPE *self, size_t index) {
    assert(self != NULL);
    assert(SLAB_NAME(check_bounds)(self, index));

    return &SLAB_NAME(get_node_mut)(self, index)->value;
}

SLAB_STATIC size_t SLAB_NAME(len)(SLAB_TYPE const *self) {
    assert(self != NULL);

    return self->len;
}

SLAB_STATIC size_t SLAB_NAME(capacity)(SLAB_TYPE const *self) {
    assert(self != NULL);

    return self->capacity;
}

SLAB_STATIC SLAB_NAME(iter_t) SLAB_NAME(iter)(SLAB_TYPE const *self, size_t start) {
    assert(self != NULL);
    assert(start == 0 || SLAB_NAME(check_bounds)(self, start));

    return (SLAB_NAME(iter_t)) {
        .slab = self,
        .current = start,

        .next = SLAB_NAME(iter_next),
    };
}

SLAB_STATIC bool SLAB_NAME(iter_next)(SLAB_NAME(iter_t) *self, size_t *result) {
    assert(self != NULL);

    do {
        if (self->current == 0) {
            return false;
        }

        if (result != NULL) {
            *result = self->current;
        }

        if (self->current == self->slab->end) {
            self->current = 0;
        } else {
            self->current =
                SLAB_NAME(get_node)(self->slab, self->current)->next;
        }
    } while (!SLAB_NAME(get_node)(self->slab, *result)->occupied);

    return true;
}

#endif // #if (SLAB_CONFIG) & COLLECTION_DEFINE

#undef SLAB_STATIC

#undef SLAB_VEC_NAME
#undef SLAB_NODE_TYPE
#undef SLAB_TYPE
#undef SLAB_NAME

#if !((SLAB_CONFIG) & COLLECTION_EXPORT_GENERIC_NAME)
#undef SLAB_GENERIC_NAME
#endif

#undef SLAB_CONFIG
#undef SLAB_LABEL
#undef SLAB_ELEMENT_TYPE

#pragma GCC diagnostic pop
