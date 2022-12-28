#pragma once

#include "url.h"

#include <common/error.h>
#include <common/loop/io.h>
#include <common/loop/loop.h>

typedef enum {
    CACHE_ENTRY_COMPLETE,
    CACHE_ENTRY_PARTIAL,
    CACHE_ENTRY_INVALID,
} cache_entry_state_t;

// An in-memory asynchronous LRU streaming HTTP cache.
typedef struct cache cache_t;

// An entry in the cache.
typedef struct cache_entry cache_entry_t;

#define ARC_ELEMENT_TYPE cache_entry_t
#define ARC_LABEL entry
#define ARC_CONFIG (COLLECTION_DECLARE)
#include <common/memory/arc.h>

// A synchronous handle to a cache entry for updating the record.
//
// Note that, unlike cache_rd_t, this is *not* a `handler_t` instance.
//
// The handle holds a strong reference to the associated cache entry, which prevents it from being
// freed.
//
// If the handle is freed before it marks the entry as completed, the associated entry is
// invalidated and will be evicted as soon as all the read handles are dropped.
typedef struct cache_wr cache_wr_t;

// An asynchronous handle to a cache entry for fetching the record.
//
// This is an instance of `handler_t` and should be used with a `loop_t`.
// It's woken up whenever the associated cache entry updates and invokes one of the callbacks for
// handling the event.
//
// The handle holds a strong reference to the associated cache entry, which prevents it from being
// freed.
typedef struct cache_rd cache_rd_t;

typedef error_t *(*cache_on_hit_cb_t)(void *data, cache_rd_t *rd);
typedef error_t *(*cache_on_miss_cb_t)(void *data, cache_rd_t *rd, cache_wr_t *wr);
typedef error_t *(*cache_on_read_cb_t)(cache_rd_t *rd, loop_t *loop);
typedef error_t *(*cache_on_update_cb_t)(cache_rd_t *rd, loop_t *loop, cache_entry_state_t state);

// Creates a new cache.
error_t *cache_new(size_t size_limit, cache_t **result);

// Frees the cache and releases references to all the contained entries.
//
// It must be ensured that no cache entries that were part of the cache are alive.
void cache_free(cache_t *self);

// Fetches an entry from the cache.
//
// If an entry is present and valid, the `on_hit` callback is invoked immediately with a newly
// created read handle provided.
//
// Otherwise the `on_miss` callback is invoked and a write handle is supplied to it.
// Note that in this case the entry must be explicitly committed to the cache once is cacheability
// is determined.
// Therefore, if multiple calls to `fetch` happen simultaneously, several new entries may be created
// for the same resource.
// In such case, the cache will remember the last committed entry.
//
// The url is copied when creating an entry, so the pointer doesn't have to stay valid after the
// call.
error_t *cache_fetch(
    cache_t *self,
    url_t const *url,
    cache_on_hit_cb_t on_hit,
    cache_on_miss_cb_t on_miss,
    void *data
);

// Sets the callback to be invoked whenever new data is appended to the entry buffer.
//
// This must be called from a synchronized context.
void cache_rd_set_on_read(cache_rd_t *self, cache_on_read_cb_t on_read);

// Sets the callack to be invoked whenever the cache entry state is updated.
//
// This must be called from a synchronized context.
void cache_rd_set_on_update(cache_rd_t *self, cache_on_update_cb_t on_update);

// Copies up to `size` bytes of unread data from the entry cache to `buf`.
//
// Returns the number of bytes actually copied.
//
// `*eof` is set to `true` if this read has returned the last unread data of a completed entry.
size_t cache_rd_read(cache_rd_t *self, char *buf, size_t size, bool *eof);

// Frees the write handle.
//
// If the entry was not marked as complete, it is invalidated as a result of this call.
void cache_wr_free(cache_wr_t *self);

// Appends a slice to the entry buffer.
//
// All the read handles are notified of the newly available data.
error_t *cache_wr_write(cache_wr_t *self, slice_t slice);

// Marks the associated cache entry as complete.
//
// All the read handles are notified of the updated entry state.
//
// This is an idempotent operation.
void cache_wr_complete(cache_wr_t *self);

// Commits the associated entry to the cache.
//
// If there already is an entry for this resource, it will be replaced if this entry has
// more written data.
//
// After this call new fetches to the resource would return a handle to this entry.
error_t *cache_wr_commit(cache_wr_t *self);

// Returns the URL associated with the cache entry.
url_t const *cache_wr_url(cache_wr_t const *self);
