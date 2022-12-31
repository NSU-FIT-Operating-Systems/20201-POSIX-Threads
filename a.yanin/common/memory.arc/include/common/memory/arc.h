#include <common/collections.h>

#ifndef ARC_ELEMENT_TYPE
#error "ARC_ELEMENT_TYPE is not defined"
#endif

#ifndef ARC_LABEL
#error "ARC_LABEL is not defined"
#endif

#ifndef ARC_CONFIG
#define ARC_CONFIG COLLECTION_DEFAULT
#endif

#if (ARC_CONFIG) & (COLLECTION_DEFINE)
#ifndef ARC_FREE_CB
#error "ARC_FREE_CB is not defined"
#endif
#endif

#ifndef ARC_GENERIC_NAME
#define ARC_GENERIC_NAME(LABEL, ITEM) \
    CONCAT(arc_, CONCAT(LABEL, CONCAT(_, ITEM)))
#endif

#define ARC_NAME(ITEM) ARC_GENERIC_NAME(ARC_LABEL, ITEM)
#define ARC_TYPE ARC_NAME(t)

#if (ARC_CONFIG) & COLLECTION_STATIC
#define ARC_STATIC [[maybe_unused]] static
#else
#define ARC_STATIC
#endif

#if (ARC_CONFIG) & COLLECTION_DECLARE

#include <stddef.h>

typedef struct ARC_NAME(struct) ARC_TYPE;

// The type of the callback passed in `ARC_FREE_CB`.
typedef void (*ARC_NAME(free_cb_t))(ARC_ELEMENT_TYPE *data);

// Creates a new arc owning `ptr`, with the reference count initialized to 1.
//
// Returns `NULL` if allocation fails.
ARC_STATIC ARC_TYPE *ARC_NAME(new)(ARC_ELEMENT_TYPE *ptr);

// Decrements the reference count.
//
// If it reaches 0, the managed object will be freed.
ARC_STATIC void ARC_NAME(free)(ARC_TYPE *self);

// Create a new arc that shares ownership with `self`.
//
// The reference count is incremented.
ARC_STATIC ARC_TYPE *ARC_NAME(share)(ARC_TYPE *self);

// Returns the pointer to the object managed by `self`.
// The reference count is not modified.
ARC_STATIC ARC_ELEMENT_TYPE *ARC_NAME(get)(ARC_TYPE *self);

// Returns the reference count.
ARC_STATIC size_t ARC_NAME(count)(ARC_TYPE const *self);

#endif // #if (ARC_CONFIG) & COLLECTION_DECLARE

#if (ARC_CONFIG) & COLLECTION_DEFINE

#include <assert.h>
#include <stdlib.h>
#include <stdatomic.h>

struct ARC_NAME(struct) {
    atomic_long ref_count;
    ARC_ELEMENT_TYPE *data;
};

ARC_STATIC ARC_TYPE *ARC_NAME(new)(ARC_ELEMENT_TYPE *ptr) {
    ARC_TYPE *self = malloc(sizeof(ARC_TYPE));
    if (self == NULL) return NULL;

    self->ref_count = 1;
    self->data = ptr;

    return self;
}

ARC_STATIC void ARC_NAME(free)(ARC_TYPE *self) {
    if (self == NULL) return;

    long ref_count = atomic_fetch_sub(&self->ref_count, 1);
    if (ref_count > 1) return;
    assert(ref_count == 1);

    ARC_NAME(free_cb_t) free_cb = (ARC_FREE_CB);
    free_cb(self->data);
    free(self);
}

ARC_STATIC ARC_TYPE *ARC_NAME(share)(ARC_TYPE *self) {
    if (self == NULL) return NULL;

    size_t ref_count = atomic_fetch_add(&self->ref_count, 1);
    assert(ref_count >= 1);

    return self;
}

ARC_STATIC ARC_ELEMENT_TYPE *ARC_NAME(get)(ARC_TYPE *self) {
    if (self == NULL) return NULL;

    assert(atomic_load(&self->ref_count) > 0);

    return self->data;
}

ARC_STATIC size_t ARC_NAME(count)(ARC_TYPE const *self) {
    return self->ref_count;
}

#endif // #if (ARC_CONFIG) & COLLECTION_DEFINE

#undef ARC_STATIC

#undef ARC_TYPE
#undef ARC_NAME

#if !((ARC_STATIC) & COLLECTION_EXPORT_GENERIC_NAME)
#undef ARC_GENERIC_NAME
#endif

#undef ARC_CONFIG
#undef ARC_FREE_CB
#undef ARC_LABEL
#undef ARC_ELEMENT_TYPE
