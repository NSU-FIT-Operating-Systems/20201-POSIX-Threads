#include "cache.h"

#include <stdatomic.h>

#ifndef WAXY_PTHREADS_DISABLED
#include <pthread.h>
#endif

#include <common/error-codes/adapter.h>
#include <common/loop/loop.h>

#include "util.h"

typedef url_t const *url_ptr_t;

static void cache_entry_free(cache_entry_t *self);

#define ARC_ELEMENT_TYPE cache_entry_t
#define ARC_LABEL entry
#define ARC_FREE_CB cache_entry_free
#define ARC_CONFIG (COLLECTION_DEFINE)
#include <common/memory/arc.h>

typedef arc_entry_t *arc_entry_ptr_t;

#define DLIST_ELEMENT_TYPE arc_entry_ptr_t
#define DLIST_LABEL entry
#define DLIST_CONFIG (COLLECTION_DECLARE | COLLECTION_DEFINE | COLLECTION_STATIC)
#include <common/collections/dlist.h>

typedef dlist_entry_node_t *dlist_entry_node_ptr_t;

#define HASH_KEY_TYPE url_ptr_t
#define HASH_VALUE_TYPE dlist_entry_node_ptr_t
#define HASH_LABEL entry
#define HASH_CONFIG (COLLECTION_DECLARE | COLLECTION_DEFINE | COLLECTION_STATIC)
#include <common/collections/hash.h>
#include <common/collections/hash/byte_hasher.h>

typedef cache_rd_t *cache_rd_ptr_t;

#define VEC_ELEMENT_TYPE cache_rd_ptr_t
#define VEC_LABEL rd
#define VEC_CONFIG (COLLECTION_DECLARE | COLLECTION_DEFINE | COLLECTION_STATIC)
#include <common/collections/vec.h>

static void url_hash(url_t const *url, byte_hasher_state_t *state) {
    byte_hasher_digest_slice(state, url->scheme.base, url->scheme.len);
    byte_hasher_digest_slice(state, url->username.base, url->username.len);
    byte_hasher_digest_slice(state, url->password.base, url->password.len);
    byte_hasher_digest_bool(state, url->host_null);

    if (!url->host_null) {
        byte_hasher_digest_slice(state, url->host.base, url->host.len);
    }

    byte_hasher_digest_bool(state, url->port_null);

    if (!url->port_null) {
        byte_hasher_digest_u16(state, url->port);
    }

    byte_hasher_digest_slice(state, url->path.base, url->path.len);
    byte_hasher_digest_bool(state, url->query_null);

    if (!url->query_null) {
        byte_hasher_digest_slice(state, url->query.base, url->query.len);
    }

    byte_hasher_digest_bool(state, url->fragment_null);

    if (!url->fragment_null) {
        byte_hasher_digest_slice(state, url->fragment.base, url->fragment.len);
    }
}

static byte_hasher_config_t const url_hasher_config_primary = {
    .seed = 164,
    .hash = (void (*)(void const *, byte_hasher_state_t *)) url_hash,
};

static byte_hasher_config_t const url_hasher_config_secondary = {
    .seed = 235,
    .hash = (void (*)(void const *, byte_hasher_state_t *)) url_hash,
};

static size_t url_ptr_hash_primary(url_t const *const *ptr, void *data) {
    return byte_hasher(*ptr, data);
}

static size_t url_ptr_hash_secondary(url_t const *const *ptr, void *data) {
    return byte_hasher_secondary(*ptr, data);
}

static hash_entry_hasher_data_t const url_ptr_primary_hasher = {
    .hasher = url_ptr_hash_primary,
    .opaque_data = (void *) &url_hasher_config_primary,
};

static hash_entry_hasher_data_t const url_ptr_secondary_hasher = {
    .hasher = url_ptr_hash_secondary,
    .opaque_data = (void *) &url_hasher_config_secondary,
};

static bool url_ptr_eq(url_t const *const *lhs, url_t const *const *rhs) {
    return url_eq(*lhs, *rhs);
}

// an `url_t *` is mapped to a `dlist_entry_node_t` via `map`
// the `dlist_entry_node` stores an `arc_entry_t`
// the `arc_entry_t` owns the `url_t`
//
// invariant: an url is present in the map iff there is exactly one `dlist_entry_node_t` that points
// to a `cache_entry_t` with that url.
struct cache {
#ifndef WAXY_PTHREADS_DISABLED
    pthread_mutex_t mtx;
#endif
    hash_entry_t map;
    dlist_entry_t entries;
    size_t size_limit;
    atomic_size_t current_size;
};

struct cache_entry {
#ifndef WAXY_PTHREADS_DISABLED
    pthread_mutex_t mtx;
#endif
    url_t url;
    vec_rd_t handles;
    string_t buf;
    cache_t *cache;
    cache_entry_state_t state;
    bool committed;
};

struct cache_wr {
    arc_entry_t *entry;
};

struct cache_rd {
    handler_t handler;
    arc_entry_t *entry;
    cache_on_read_cb_t on_read;
    cache_on_update_cb_t on_update;
    size_t count;
    cache_entry_state_t last_state;
    bool registered;
};

error_t *cache_new(size_t size_limit, cache_t **result) {
    error_t *err = NULL;

    cache_t *self = calloc(1, sizeof(cache_t));
    err = error_wrap("Could not allocate memory for the cache", OK_IF(self != NULL));
    if (err) goto calloc_fail;

#ifndef WAXY_PTHREADS_DISABLED
    pthread_mutexattr_t mtx_attr;
    err = error_wrap("Could not initialize mutex attributes", error_from_errno(
        pthread_mutexattr_init(&mtx_attr)));
    if (err) goto mtx_attr_init_fail;

    err = error_wrap("Could not initialize a mutex", error_from_errno(
        pthread_mutex_init(&self->mtx, &mtx_attr)));
    pthread_mutexattr_destroy(&mtx_attr);
    if (err) goto mtx_init_fail;
#endif

    err = error_from_common(hash_entry_new(
        url_ptr_primary_hasher, url_ptr_secondary_hasher,
        url_ptr_eq, &self->map
    ));
    if (err) goto hash_new_fail;

    self->entries = dlist_entry_new();
    self->size_limit = size_limit;
    self->current_size = 0;

    *result = self;

    return err;

hash_new_fail:
#ifndef WAXY_PTHREADS_DISABLED
    pthread_mutex_destroy(&self->mtx);

mtx_init_fail:
mtx_attr_init_fail:
#endif
    free(self);

calloc_fail:
    return err;
}

void cache_free(cache_t *self) {
    hash_entry_free(&self->map);

    for (dlist_entry_node_t *node = dlist_entry_head_mut(&self->entries);
            node != NULL;
            node = dlist_entry_next_mut(node)) {
        arc_entry_t *arc = *dlist_entry_get_mut(node);
        arc_entry_free(arc);
    }

    dlist_entry_free(&self->entries);

#ifndef WAXY_PTHREADS_DISABLED
    error_assert(error_from_errno(pthread_mutex_destroy(&self->mtx)));
#endif

    free(self);
}

static error_t *cache_entry_new_rd(arc_entry_t *arc, cache_rd_t **result);
static error_t *cache_entry_new_rd_unsync(arc_entry_t *arc, cache_rd_t **result);

// Removes an entry pointed to by `node` from the cache.
//
// Returns its last recorded size.
static size_t cache_remove_entry_unsync(cache_t *self, dlist_entry_node_t *node) {
    arc_entry_t *arc = dlist_entry_remove(&self->entries, node);
    cache_entry_t *entry = arc_entry_get(arc);

    // this ensures cache_wr_write doesn't update self->current_size anymore
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_lock(&entry->mtx);
#endif
    entry->cache = NULL;
    size_t size = string_len(&entry->buf);
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&entry->mtx);
#endif

    dlist_entry_node_t *removed_node = NULL;
    // only fails if the key was not found -- our invariant ensures this can't happen
    error_assert(error_from_common(
        hash_entry_remove(&self->map, &(url_t const *) { &entry->url }, NULL, &removed_node)));

    arc_entry_free(arc);

    assert(size <= self->current_size);
    assert(node == removed_node);

    self->current_size -= size;

    return size;
}

static void cache_evict_if_necessary_unsync(cache_t *self) {
    while (self->current_size > self->size_limit) {
        dlist_entry_node_t *head = dlist_entry_head_mut(&self->entries);

        if (head == NULL) {
            break;
        }

        cache_remove_entry_unsync(self, head);
    }
}

static error_t *cache_create_entry_unsync(
    cache_t *self,
    url_t const *url,
    cache_rd_t **result_rd,
    cache_wr_t **result_wr
) {
    error_t *err = NULL;

    cache_entry_t *entry = calloc(1, sizeof(cache_entry_t));
    err = error_wrap("Could not allocate an entry", OK_IF(entry != NULL));
    if (err) goto entry_calloc_fail;

    cache_wr_t *wr = calloc(1, sizeof(cache_wr_t));
    err = error_wrap("Could not allocate a write handle", OK_IF(wr != NULL));
    if (err) goto wr_calloc_fail;

#ifndef WAXY_PTHREADS_DISABLED
    pthread_mutexattr_t mtx_attr;
    err = error_wrap("Could not initialize mutex attributes", error_from_errno(
        pthread_mutexattr_init(&mtx_attr)));
    if (err) goto mtxattr_init_fail;

    err = error_wrap("Could not initialize a mutex", error_from_errno(
        pthread_mutex_init(&entry->mtx, &mtx_attr)));
    pthread_mutexattr_destroy(&mtx_attr);
    if (err) goto mtx_init_fail;
#endif

    err = error_wrap("Could not copy the URL", url_copy(url, &entry->url));
    if (err) goto url_copy_fail;

    entry->handles = vec_rd_new();

    err = error_wrap("Could not create a buffer", error_from_common(
        string_new(&entry->buf)));
    if (err) goto string_new_fail;

    entry->cache = self;
    entry->state = CACHE_ENTRY_PARTIAL;
    entry->committed = false;

    arc_entry_t *arc = arc_entry_new(entry);
    err = error_wrap("Could not allocate an entry", OK_IF(arc != NULL));
    if (err) goto arc_new_fail;

    cache_rd_t *rd = NULL;
    err = cache_entry_new_rd(arc_entry_share(arc), &rd);
    if (err) goto new_rd_fail;

    wr->entry = arc_entry_share(arc);

    *result_rd = rd;
    *result_wr = wr;
    arc_entry_free(arc);

    return err;

new_rd_fail:
    arc_entry_free(arc);

    return err;

arc_new_fail:
    string_free(&entry->buf);

string_new_fail:
    vec_rd_free(&entry->handles);
    string_free(&entry->url.buf);

url_copy_fail:
#ifndef WAXY_PTHREADS_DISABLED
    error_assert(error_from_errno(pthread_mutex_destroy(&entry->mtx)));

mtx_init_fail:
mtxattr_init_fail:
#endif
    free(wr);

wr_calloc_fail:
    free(entry);

entry_calloc_fail:
    return err;
}

static error_t *cache_create_handle_unsync(
    cache_t *self,
    dlist_entry_node_t *node,
    url_t const *url,
    cache_rd_t **result_rd,
    cache_wr_t **result_wr
) {
    error_t *err = NULL;

    arc_entry_t *arc = *dlist_entry_get_mut(node);
    cache_entry_t *entry = arc_entry_get(arc);
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_lock(&entry->mtx);
#endif

    if (entry->state == CACHE_ENTRY_INVALID) {
#ifndef WAXY_PTHREADS_DISABLED
        assert_mutex_unlock(&entry->mtx);
#endif

        cache_evict_if_necessary_unsync(self);

        return cache_create_entry_unsync(self, url, result_rd, result_wr);
    }

    cache_rd_t *rd = NULL;
    err = cache_entry_new_rd_unsync(arc_entry_share(arc), &rd);
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&entry->mtx);
#endif
    if (err) goto new_rd_fail;

    dlist_entry_move_before(&self->entries, node, NULL);

    *result_rd = rd;
    *result_wr = NULL;

    return err;

new_rd_fail:
    return err;
}

error_t *cache_fetch(
    cache_t *self,
    url_t const *url,
    cache_on_hit_cb_t on_hit,
    cache_on_miss_cb_t on_miss,
    void *data
) {
    error_t *err = NULL;

#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_lock(&self->mtx);
#endif

    dlist_entry_node_t **ptr = hash_entry_get_mut(&self->map, &url);
    cache_rd_t *rd = NULL;
    cache_wr_t *wr = NULL;

    if (ptr == NULL) {
        cache_evict_if_necessary_unsync(self);

        err = cache_create_entry_unsync(self, url, &rd, &wr);
    } else {
        err = cache_create_handle_unsync(self, *ptr, url, &rd, &wr);
    }

#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&self->mtx);
#endif

    if (err) goto create_fail;

    if (wr != NULL) {
        err = on_miss(data, rd, wr);
    } else {
        err = on_hit(data, rd);
    }

create_fail:
    return err;
}

static void rd_free(cache_rd_t *self) {
    if (self->registered) {
        arc_entry_t *arc = self->entry;
        cache_entry_t *entry = arc_entry_get(arc);
#ifndef WAXY_PTHREADS_DISABLED
        assert_mutex_lock(&entry->mtx);
#endif

        for (size_t i = 0; i < vec_rd_len(&entry->handles); ++i) {
            if (*vec_rd_get(&entry->handles, i) == self) {
                log_printf(LOG_DEBUG, "Removed a cache_rd_t from entry->handles");
                vec_rd_remove(&entry->handles, i);

                break;
            }
        }

#ifndef WAXY_PTHREADS_DISABLED
        assert_mutex_unlock(&entry->mtx);
#endif
    }

    arc_entry_free(self->entry);
}

static error_t *rd_process(cache_rd_t *self, loop_t *loop, poll_flags_t) {
    error_t *err = NULL;

    arc_entry_t *arc = self->entry;
    cache_entry_t *entry = arc_entry_get(arc);
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_lock(&entry->mtx);
#endif

    size_t new_len = string_len(&entry->buf);
    cache_entry_state_t state = entry->state;

#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&entry->mtx);
#endif

    log_printf(LOG_DEBUG, "rd_process: new_len = %zu, state = %d, self->count = %zu, self->last_state = %d",
        new_len, state, self->count, self->last_state);

    if (new_len > self->count || (state != self->last_state && state == CACHE_ENTRY_COMPLETE)) {
        log_printf(LOG_DEBUG, "rd_process: Calling on_read");

        err = self->on_read(self, loop);
        if (err) return err;

        if (self->count < new_len) {
            handler_force(&self->handler);
        }
    }

    if (state != self->last_state) {
        log_printf(LOG_DEBUG, "rd_process: Calling on_update");
        err = self->on_update(self, loop, state);
        if (err) return err;

        self->last_state = state;
    }

    return err;
}

static handler_vtable_t const rd_vtable = {
    .free = (handler_vtable_free_t) rd_free,
    .on_error = NULL,
    .process = (handler_vtable_process_t) rd_process,
};

// the arc is owned
static error_t *cache_entry_new_rd_unsync(arc_entry_t *arc, cache_rd_t **result) {
    error_t *err = NULL;

    cache_entry_t *entry = arc_entry_get(arc);

    cache_rd_t *rd = calloc(1, sizeof(cache_rd_t));
    err = error_wrap("Could not allocate a read handle", OK_IF(rd != NULL));
    if (err) goto calloc_fail;

    handler_init(&rd->handler, &rd_vtable, -1);
    rd->entry = arc_entry_share(arc);
    rd->on_read = NULL;
    rd->on_update = NULL;
    rd->count = 0;
    rd->last_state = -1;
    rd->registered = false;

    log_printf(LOG_DEBUG, "Registering the newly created rd_handle");
    err = error_wrap("Could not register the created handle", error_from_common(
        vec_rd_push(&entry->handles, rd)));
    if (err) goto rd_push_fail;

    rd->registered = true;

    if (string_len(&entry->buf) > 0) {
        handler_force(&rd->handler);
    }

    *result = rd;
    arc_entry_free(arc);

    return err;

rd_push_fail:
    handler_free(&rd->handler);

calloc_fail:
    arc_entry_free(arc);

    return err;
}

// the mutex must be unlocked
// the arc is owned
static error_t *cache_entry_new_rd(arc_entry_t *arc, cache_rd_t **result) {
    error_t *err = NULL;

#ifndef WAXY_PTHREADS_DISABLED
    cache_entry_t *entry = arc_entry_get(arc);
    assert_mutex_lock(&entry->mtx);
#endif

    err = cache_entry_new_rd_unsync(arc_entry_share(arc), result);

#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&entry->mtx);
#endif
    arc_entry_free(arc);

    return err;
}

static void cache_entry_free(cache_entry_t *self) {
#ifndef WAXY_PTHREADS_DISABLED
    pthread_mutex_destroy(&self->mtx);
#endif
    string_free(&self->url.buf);
    vec_rd_free(&self->handles);
    string_free(&self->buf);
    self->state = CACHE_ENTRY_INVALID;
    free(self);
}

static void cache_entry_wake_unsync(cache_entry_t *entry) {
    for (size_t i = 0; i < vec_rd_len(&entry->handles); ++i) {
        cache_rd_t *handle = *vec_rd_get_mut(&entry->handles, i);
        log_printf(LOG_DEBUG, "Waking up %p", (void *) handle);
        handler_force(&handle->handler);
    }
}

static void cache_entry_set_state_unsync(cache_entry_t *entry, cache_entry_state_t state) {
    if (entry->state == state) {
        return;
    }

    if (entry->state == CACHE_ENTRY_INVALID) {
        log_printf(
            LOG_WARN,
            "Tried to change the state of an invalidated cache entry to %d",
            state
        );

        return;
    }

    entry->state = state;
    cache_entry_wake_unsync(entry);
}

void cache_rd_set_on_read(cache_rd_t *self, cache_on_read_cb_t on_read) {
    self->on_read = on_read;
}

void cache_rd_set_on_update(cache_rd_t *self, cache_on_update_cb_t on_update) {
    self->on_update = on_update;
}

size_t cache_rd_read(cache_rd_t *self, char *buf, size_t size, bool *eof) {
    arc_entry_t *arc = self->entry;
    cache_entry_t *entry = arc_entry_get(arc);
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_lock(&entry->mtx);
#endif

    size_t unread_count = string_len(&entry->buf) - self->count;

    if (size > unread_count) {
        size = unread_count;
    }

    memcpy(buf, string_as_cptr(&entry->buf) + self->count, size);
    self->count += size;

    if (entry->state != CACHE_ENTRY_PARTIAL && self->count >= string_len(&entry->buf)) {
        *eof = true;
    }

#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&entry->mtx);
#endif

    return size;
}

void cache_wr_free(cache_wr_t *self) {
    if (self == NULL) return;

    arc_entry_t *arc = self->entry;
    cache_entry_t *entry = arc_entry_get(arc);
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_lock(&entry->mtx);
#endif

    if (entry->state == CACHE_ENTRY_PARTIAL) {
        cache_entry_set_state_unsync(entry, CACHE_ENTRY_INVALID);
    }

#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&entry->mtx);
#endif
    arc_entry_free(arc);
    free(self);
}

error_t *cache_wr_write(cache_wr_t *self, slice_t slice) {
    error_t *err = NULL;

    if (slice.len == 0) {
        return err;
    }

    arc_entry_t *arc = self->entry;
    cache_entry_t *entry = arc_entry_get(arc);
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_lock(&entry->mtx);
#endif

    err = error_wrap("Writing to a complete entry", OK_IF(entry->state != CACHE_ENTRY_COMPLETE));
    if (err) goto complete_fail;

    err = error_from_common(string_append_slice(&entry->buf, slice.base, slice.len));
    if (err) goto append_fail;

    cache_t *cache = entry->cache;

    if (cache != NULL && entry->committed) {
        atomic_fetch_add(&cache->current_size, slice.len);
    }

    cache_entry_wake_unsync(entry);

append_fail:
complete_fail:
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&entry->mtx);
#endif

    return err;
}

void cache_wr_complete(cache_wr_t *self) {
    arc_entry_t *arc = self->entry;
    cache_entry_t *entry = arc_entry_get(arc);
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_lock(&entry->mtx);
#endif

    cache_entry_set_state_unsync(entry, CACHE_ENTRY_COMPLETE);

#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&entry->mtx);
#endif
}

error_t *cache_wr_commit(cache_wr_t *self) {
    error_t *err = NULL;

    arc_entry_t *arc = self->entry;
    cache_entry_t *entry = arc_entry_get(arc);
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_lock(&entry->mtx);
#endif

    if (entry->committed) {
        goto committed;
    }

    cache_t *cache = entry->cache;
    assert(cache != NULL);

    // There are no references to this entry in the cache yet.
    // Therefore, the cache can't possibly be able to lock entry->mtx, which would cause a deadlock.
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_lock(&cache->mtx);
#endif

    dlist_entry_node_t **stored_node_ptr = hash_entry_get_mut(
        &cache->map,
        &(url_t const *) { &entry->url }
    );

    if (stored_node_ptr != NULL) {
        dlist_entry_node_t *stored_node = *stored_node_ptr;
        arc_entry_t *stored_arc = *dlist_entry_get_mut(stored_node);
        cache_entry_t *stored_entry = arc_entry_get(stored_arc);

        // deadlock-safe for the same reason as above: nobody knows about us yet
#ifndef WAXY_PTHREADS_DISABLED
        assert_mutex_lock(&stored_entry->mtx);
#endif
        size_t stored_size = string_len(&stored_entry->buf);
#ifndef WAXY_PTHREADS_DISABLED
        assert_mutex_unlock(&stored_entry->mtx);
#endif

        if (stored_size < string_len(&entry->buf)) {
            goto success;
        }

        cache_remove_entry_unsync(cache, stored_node);
    }

    dlist_entry_node_t *node = NULL;
    arc_entry_t *arc_shared = arc_entry_share(arc);
    err = error_wrap("Could not register the entry in the cache", error_from_common(
        dlist_entry_append(&cache->entries, arc_shared, &node)));
    if (err) goto dlist_append_fail;

    arc_shared = NULL;

    err = error_wrap("Could not bind the URL to the entry", error_from_common(
        hash_entry_insert(&cache->map, &entry->url, node)));
    if (err) goto hash_insert_fail;

    cache->current_size += string_len(&entry->buf);
    entry->committed = true;

    goto success;

hash_insert_fail:
    arc_shared = dlist_entry_remove(&cache->entries, node);

dlist_append_fail:
    arc_entry_free(arc_shared);

success:
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&cache->mtx);
#endif

committed:
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&entry->mtx);
#endif

    return err;
}

url_t const *cache_wr_url(cache_wr_t const *self) {
    return &arc_entry_get(self->entry)->url;
}
