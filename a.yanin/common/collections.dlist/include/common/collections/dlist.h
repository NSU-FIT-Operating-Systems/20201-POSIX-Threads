#include "common/collections.h"

#pragma GCC diagnostic push

#ifndef DLIST_ELEMENT_TYPE
#error "DLIST_ELEMENT_TYPE is not defined"
#endif

#ifndef DLIST_LABEL
#error "DLIST_LABEL is not defined"
#endif

#ifndef DLIST_CONFIG
#define DLIST_CONFIG COLLECTION_DEFAULT
#endif

#ifndef DLIST_GENERIC_NAME
#define DLIST_GENERIC_NAME(LABEL, ITEM) \
    CONCAT(dlist_, CONCAT(LABEL, CONCAT(_, ITEM)))
#endif

#define DLIST_NAME(ITEM) DLIST_GENERIC_NAME(DLIST_LABEL, ITEM)
#define DLIST_TYPE DLIST_NAME(t)

#define DLIST_NODE_TYPE DLIST_NAME(node_t)

#if (DLIST_CONFIG) & COLLECTION_STATIC
#define DLIST_STATIC static
#pragma GCC diagnostic ignored "-Wunused-function"
#else
#define DLIST_STATIC
#endif

#if (DLIST_CONFIG) & COLLECTION_DECLARE

#include <stddef.h>

#include "common/error-codes/error-codes.h"

typedef struct DLIST_NAME(node) DLIST_NODE_TYPE;

struct DLIST_NAME(node) {
    DLIST_ELEMENT_TYPE value;

    DLIST_NODE_TYPE *prev;
    DLIST_NODE_TYPE *next;
};

typedef struct DLIST_NAME(t) {
    DLIST_NODE_TYPE *head;
    DLIST_NODE_TYPE *end;
    size_t len;
} DLIST_TYPE;

DLIST_STATIC DLIST_TYPE DLIST_NAME(new)(void);
DLIST_STATIC void DLIST_NAME(free)(DLIST_TYPE *self);

DLIST_STATIC common_error_code_t DLIST_NAME(insert_after)(
    DLIST_TYPE *self,
    DLIST_ELEMENT_TYPE value,
    DLIST_NODE_TYPE *node,
    DLIST_NODE_TYPE **result
);
DLIST_STATIC common_error_code_t DLIST_NAME(insert_before)(
    DLIST_TYPE *self,
    DLIST_ELEMENT_TYPE value,
    DLIST_NODE_TYPE *node,
    DLIST_NODE_TYPE **result
);
DLIST_STATIC common_error_code_t DLIST_NAME(append)(
    DLIST_TYPE *self,
    DLIST_ELEMENT_TYPE value,
    DLIST_NODE_TYPE **result
);

DLIST_STATIC DLIST_TYPE DLIST_NAME(pluck)(
    DLIST_TYPE *self,
    DLIST_NODE_TYPE *node
);
DLIST_STATIC DLIST_ELEMENT_TYPE DLIST_NAME(remove)(
    DLIST_TYPE *self,
    DLIST_NODE_TYPE *node
);

DLIST_STATIC DLIST_NODE_TYPE const *DLIST_NAME(next)(
    DLIST_NODE_TYPE const *node
);
DLIST_STATIC DLIST_NODE_TYPE const *DLIST_NAME(prev)(
    DLIST_NODE_TYPE const *node
);
DLIST_STATIC DLIST_NODE_TYPE const *DLIST_NAME(head)(DLIST_TYPE const *self);
DLIST_STATIC DLIST_NODE_TYPE const *DLIST_NAME(end)(DLIST_TYPE const *self);

DLIST_STATIC DLIST_NODE_TYPE *DLIST_NAME(next_mut)(DLIST_NODE_TYPE *node);
DLIST_STATIC DLIST_NODE_TYPE *DLIST_NAME(prev_mut)(DLIST_NODE_TYPE *node);
DLIST_STATIC DLIST_NODE_TYPE *DLIST_NAME(head_mut)(DLIST_TYPE *self);
DLIST_STATIC DLIST_NODE_TYPE *DLIST_NAME(end_mut)(DLIST_TYPE *self);

DLIST_STATIC DLIST_ELEMENT_TYPE const *DLIST_NAME(get)(
    DLIST_NODE_TYPE const *node
);
DLIST_STATIC DLIST_ELEMENT_TYPE *DLIST_NAME(get_mut)(DLIST_NODE_TYPE *node);

DLIST_STATIC size_t DLIST_NAME(len)(DLIST_TYPE const *self);

DLIST_STATIC void DLIST_NAME(concat)(DLIST_TYPE *self, DLIST_TYPE other);
DLIST_STATIC void DLIST_NAME(swap)(DLIST_TYPE *self, DLIST_NODE_TYPE *lhs, DLIST_NODE_TYPE *rhs);
DLIST_STATIC void DLIST_NAME(move_before)(
    DLIST_TYPE *self,
    DLIST_NODE_TYPE *node,
    DLIST_NODE_TYPE *before
);
DLIST_STATIC void DLIST_NAME(move_after)(
    DLIST_TYPE *self,
    DLIST_NODE_TYPE *node,
    DLIST_NODE_TYPE *before
);

#endif // #if (DLIST_CONFIG) & COLLECTION_DECLARE

#if (DLIST_CONFIG) & COLLECTION_DEFINE

#include <assert.h>
#include <stdlib.h>

#include "common/error-codes/macros.h"

DLIST_STATIC DLIST_TYPE DLIST_NAME(new)(void) {
    return (DLIST_TYPE) {
        .head = NULL,
        .end = NULL,
        .len = 0,
    };
}

DLIST_STATIC void DLIST_NAME(free)(DLIST_TYPE *self) {
    assert(self != NULL);

    DLIST_NODE_TYPE *node = self->head;

    while (node != NULL) {
        DLIST_NODE_TYPE *next = node->next;
        free(node);
        node = next;
    }
}

static bool DLIST_NAME(contains)(
    DLIST_TYPE const *self,
    DLIST_NODE_TYPE const *node
) {
    assert(self != NULL);
    assert(node != NULL);

    for (DLIST_NODE_TYPE const *n = self->head;
            n != NULL;
            n = n->next) {
        if (n == node) {
            return true;
        }
    }

    return false;
}

static void DLIST_NAME(link)(DLIST_NODE_TYPE *prev, DLIST_NODE_TYPE *next) {
    assert(prev != NULL || next != NULL);

    if (prev != NULL) {
        prev->next = next;
    }

    if (next != NULL) {
        next->prev = prev;
    }
}

static void DLIST_NAME(unlink_prev)(DLIST_NODE_TYPE *next) {
    assert(next != NULL);
    assert(next->next != next);

    if (next->prev != NULL) {
        assert(next->prev->next == next);
        next->prev->next = next->next;
    }

    next->prev = NULL;
}

static void DLIST_NAME(unlink_both)(DLIST_NODE_TYPE *node) {
    assert(node != NULL);

    DLIST_NODE_TYPE *prev = node->prev;
    DLIST_NODE_TYPE *next = node->next;

    if (prev != NULL) {
        prev->next = next;
    }

    if (next != NULL) {
        next->prev = prev;
    }

    node->prev = node->next = NULL;
}

static common_error_code_t DLIST_NAME(new_node)(
    DLIST_ELEMENT_TYPE value,
    DLIST_NODE_TYPE **result
) {
    assert(result != NULL);

    *result = malloc(sizeof(DLIST_NODE_TYPE));

    if (*result == NULL) {
        return COMMON_ERROR_CODE_MEMORY_ALLOCATION_FAILURE;
    }

    **result = (DLIST_NODE_TYPE) {
        .value = value,
        .prev = NULL,
        .next = NULL,
    };

    return COMMON_ERROR_CODE_OK;
}

DLIST_STATIC common_error_code_t DLIST_NAME(insert_after)(
    DLIST_TYPE *self,
    DLIST_ELEMENT_TYPE value,
    DLIST_NODE_TYPE *prev,
    DLIST_NODE_TYPE **result
) {
    assert(self != NULL);

    common_error_code_t code = COMMON_ERROR_CODE_OK;

    DLIST_NODE_TYPE *node = NULL;
    GOTO_ON_ERROR(code = DLIST_NAME(new_node)(value, &node), alloc_fail);

    DLIST_NODE_TYPE *next;

    if (prev != NULL) {
        assert(DLIST_NAME(contains)(self, prev));
        next = prev->next;
    } else {
        next = self->head;
    }

    if (next != NULL) {
        DLIST_NAME(unlink_prev)(next);
    }

    DLIST_NAME(link)(prev, node);
    DLIST_NAME(link)(node, next);

    if (prev == NULL) {
        self->head = node;
    }

    if (next == NULL) {
        self->end = node;
    }

    if (result != NULL) {
        *result = node;
    }

    ++self->len;

alloc_fail:
    return code;
}

DLIST_STATIC common_error_code_t DLIST_NAME(insert_before)(
    DLIST_TYPE *self,
    DLIST_ELEMENT_TYPE value,
    DLIST_NODE_TYPE *next,
    DLIST_NODE_TYPE **result
) {
    assert(self != NULL);

    if (next == NULL) {
        return DLIST_NAME(append)(self, value, result);
    }

    return DLIST_NAME(insert_after)(self, value, next->prev, result);
}

DLIST_STATIC common_error_code_t DLIST_NAME(append)(
    DLIST_TYPE *self,
    DLIST_ELEMENT_TYPE value,
    DLIST_NODE_TYPE **result
) {
    assert(self != NULL);

    return DLIST_NAME(insert_after)(self, value, self->end, result);
}

DLIST_STATIC DLIST_TYPE DLIST_NAME(pluck)(
    DLIST_TYPE *self,
    DLIST_NODE_TYPE *node
) {
    assert(self != NULL);
    assert(node != NULL);
    assert(DLIST_NAME(contains)(self, node));

    if (node == self->head) {
        self->head = self->head->next;
    }

    if (node == self->end) {
        self->end = self->end->prev;
    }

    DLIST_NAME(unlink_both)(node);
    --self->len;

    assert(!DLIST_NAME(contains)(self, node));

    return (DLIST_TYPE) {
        .head = node,
        .end = node,
        .len = 1,
    };
}

DLIST_STATIC DLIST_ELEMENT_TYPE DLIST_NAME(remove)(
    DLIST_TYPE *self,
    DLIST_NODE_TYPE *node
) {
    assert(self != NULL);
    assert(node != NULL);
    assert(DLIST_NAME(contains)(self, node));

    DLIST_NAME(pluck)(self, node);
    DLIST_ELEMENT_TYPE value = node->value;

    free(node);

    return value;
}

DLIST_STATIC DLIST_NODE_TYPE const *DLIST_NAME(next)(
    DLIST_NODE_TYPE const *node
) {
    if (node == NULL) {
        return NULL;
    } else {
        return node->next;
    }
}

DLIST_STATIC DLIST_NODE_TYPE const *DLIST_NAME(prev)(
    DLIST_NODE_TYPE const *node
) {
    if (node == NULL) {
        return NULL;
    } else {
        return node->prev;
    }
}

DLIST_STATIC DLIST_NODE_TYPE const *DLIST_NAME(head)(DLIST_TYPE const *self) {
    assert(self != NULL);

    return self->head;
}

DLIST_STATIC DLIST_NODE_TYPE const *DLIST_NAME(end)(DLIST_TYPE const *self) {
    assert(self != NULL);

    return self->end;
}

DLIST_STATIC DLIST_NODE_TYPE *DLIST_NAME(next_mut)(DLIST_NODE_TYPE *node) {
    if (node == NULL) {
        return NULL;
    } else {
        return node->next;
    }
}

DLIST_STATIC DLIST_NODE_TYPE *DLIST_NAME(prev_mut)(DLIST_NODE_TYPE *node) {
    if (node == NULL) {
        return NULL;
    } else {
        return node->prev;
    }
}

DLIST_STATIC DLIST_NODE_TYPE *DLIST_NAME(head_mut)(DLIST_TYPE *self) {
    assert(self != NULL);

    return self->head;
}

DLIST_STATIC DLIST_NODE_TYPE *DLIST_NAME(end_mut)(DLIST_TYPE *self) {
    assert(self != NULL);

    return self->end;
}

DLIST_STATIC DLIST_ELEMENT_TYPE const *DLIST_NAME(get)(
    DLIST_NODE_TYPE const *node
) {
    assert(node != NULL);

    return &node->value;
}

DLIST_STATIC DLIST_ELEMENT_TYPE *DLIST_NAME(get_mut)(DLIST_NODE_TYPE *node) {
    assert(node != NULL);

    return &node->value;
}

DLIST_STATIC size_t DLIST_NAME(len)(DLIST_TYPE const *self) {
    assert(self != NULL);

    return self->len;
}

DLIST_STATIC void DLIST_NAME(concat)(DLIST_TYPE *self, DLIST_TYPE other) {
    assert(self != NULL);

    if (other.len == 0) {
        return;
    }

    if (self->len == 0) {
        *self = other;

        return;
    }

    DLIST_NAME(link)(self->end, other.head);
    self->len += other.len;
    self->end = other.end;
}

DLIST_STATIC void DLIST_NAME(swap)(DLIST_TYPE *self, DLIST_NODE_TYPE *lhs, DLIST_NODE_TYPE *rhs) {
    assert(self != NULL);
    assert(lhs != NULL);
    assert(rhs != NULL);
    assert(DLIST_NAME(contains)(self, lhs));
    assert(DLIST_NAME(contains)(self, rhs));

    if (lhs == rhs) {
        return;
    }

    DLIST_NODE_TYPE *lhs_prev = DLIST_NAME(prev_mut)(lhs);
    DLIST_NODE_TYPE *lhs_next = DLIST_NAME(next_mut)(lhs);
    DLIST_NODE_TYPE *rhs_prev = DLIST_NAME(prev_mut)(rhs);
    DLIST_NODE_TYPE *rhs_next = DLIST_NAME(next_mut)(rhs);

    if (rhs_next == lhs) {
        DLIST_NODE_TYPE *tmp = rhs;
        rhs = lhs;
        lhs = tmp;
    }

    // lhs_prev <-> lhs <-> lhs_next
    // rhs_prev <-> rhs <-> rhs_next

    DLIST_NAME(link)(lhs_prev, rhs);
    DLIST_NAME(link)(lhs, rhs_next);

    if (lhs_next == rhs) {
        DLIST_NAME(link)(rhs, lhs);
    } else {
        DLIST_NAME(link)(rhs, lhs_next);
        DLIST_NAME(link)(rhs_prev, lhs);
    }

    // lhs_prev <-> rhs <-> lhs_next
    // rhs_prev <-> lhs <-> rhs_next

    if (self->head == lhs) {
        self->head = rhs;
    } else if (self->head == rhs) {
        self->head = lhs;
    }

    if (self->end == lhs) {
        self->end = rhs;
    } else if (self->end == rhs) {
        self->end = lhs;
    }
}

static bool DLIST_NAME(is_valid)(DLIST_TYPE const *self) {
    size_t actual_len = 0;
    DLIST_NODE_TYPE const *actual_end = NULL;

    for (DLIST_NODE_TYPE const *node = self->head;
            node != NULL;
            node = node->next, ++actual_len) {
        assert(node->next == NULL || node->next->prev == node);
        assert(node->prev == NULL || node->prev->next == node);
        actual_end = node;
    }

    assert(self->len == actual_len);
    assert(self->end == actual_end);

    return true;
}

DLIST_STATIC void DLIST_NAME(move_before)(
    DLIST_TYPE *self,
    DLIST_NODE_TYPE *node,
    DLIST_NODE_TYPE *before
) {
    assert(self != NULL);
    assert(node != NULL);
    assert(DLIST_NAME(contains)(self, node));
    assert(before == NULL || DLIST_NAME(contains)(self, before));
    assert(DLIST_NAME(is_valid)(self));

    if (before == node || before == DLIST_NAME(next)(node)) {
        return;
    }

    if (before == NULL) {
        DLIST_NAME(move_after)(self, node, DLIST_NAME(end_mut)(self));

        return;
    }

    DLIST_NODE_TYPE *node_prev = DLIST_NAME(prev_mut)(node);
    DLIST_NODE_TYPE *node_next = DLIST_NAME(next_mut)(node);
    DLIST_NODE_TYPE *before_prev = DLIST_NAME(prev_mut)(before);

    DLIST_NAME(unlink_both)(node);
    DLIST_NAME(link)(before_prev, node);
    DLIST_NAME(link)(node, before);

    if (node == self->head) {
        self->head = node_next;
    } else if (before == self->head) {
        self->head = node;
    }

    if (node == self->end) {
        self->end = node_prev;
    }

    assert(DLIST_NAME(is_valid)(self));
}

DLIST_STATIC void DLIST_NAME(move_after)(
    DLIST_TYPE *self,
    DLIST_NODE_TYPE *node,
    DLIST_NODE_TYPE *after
) {
    assert(self != NULL);
    assert(node != NULL);
    assert(DLIST_NAME(contains)(self, node));
    assert(after == NULL || DLIST_NAME(contains)(self, after));
    assert(DLIST_NAME(is_valid)(self));

    if (after == node || after == DLIST_NAME(prev)(node)) {
        return;
    }

    if (after == NULL) {
        DLIST_NAME(move_after)(self, node, DLIST_NAME(head_mut)(self));

        return;
    }

    DLIST_NODE_TYPE *node_prev = DLIST_NAME(prev_mut)(node);
    DLIST_NODE_TYPE *node_next = DLIST_NAME(next_mut)(node);
    DLIST_NODE_TYPE *after_next = DLIST_NAME(next_mut)(after);

    DLIST_NAME(unlink_both)(node);
    DLIST_NAME(link)(after, node);
    DLIST_NAME(link)(node, after_next);

    if (node == self->head) {
        self->head = node_next;
    }

    if (node == self->end) {
        self->end = node_prev;
    } else if (after == self->end) {
        self->end = node;
    }
    assert(DLIST_NAME(is_valid)(self));
}

#endif // #if (DLIST_CONFIG) & COLLECTION_DEFINE

#undef DLIST_STATIC

#undef DLIST_NODE_TYPE

#undef DLIST_TYPE
#undef DLIST_NAME

#if !((DLIST_CONFIG) & COLLECTION_EXPORT_GENERIC_NAME)
#undef DLIST_GENERIC_NAME
#endif

#undef DLIST_CONFIG
#undef DLIST_LABEL
#undef DLIST_ELEMENT_TYPE

#pragma GCC diagnostic pop
