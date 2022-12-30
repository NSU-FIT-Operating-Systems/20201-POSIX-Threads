#include "client.h"

#include <assert.h>
#include <stdlib.h>

#ifndef WAXY_PTHREADS_DISABLED
#include <pthread.h>
#endif

#include <picohttpparser/picohttpparser.h>

#include <common/error-codes/adapter.h>

#include "cache.h"
#include "util.h"
#include "upstream.h"

#define SERVER_HEADER "Server: waxy\r\n"

enum {
    MAX_HEADERS = 512,
    // have you ever seen an HTTP GET request larger than 16 MiB? me neither.
    MAX_REQUEST_SIZE = 16 * 1024 * 1024,
    CACHE_BUFFER_SIZE = 4 * 1024 * 1024,
};

// This struct is owned by the TCP handler (`tcp`).
// A reference to it is kept as the custom data of `rd`.
typedef struct {
#ifndef WAXY_PTHREADS_DISABLED
    pthread_mutex_t mtx;
#endif
    cache_t *cache;
    string_t buf;
    struct phr_header *headers;
    tcp_handler_t *tcp;
    cache_rd_t *rd;
} client_ctx_t;

static void client_ctx_free(client_ctx_t *ctx) {
#ifndef WAXY_PTHREADS_DISABLED
    pthread_mutex_destroy(&ctx->mtx);
#endif

    string_free(&ctx->buf);
    free(ctx->headers);
    free(ctx);
}

#define ARC_ELEMENT_TYPE client_ctx_t
#define ARC_LABEL ctx
#define ARC_FREE_CB client_ctx_free
#define ARC_CONFIG (COLLECTION_DECLARE | COLLECTION_DEFINE | COLLECTION_STATIC)
#include <common/memory/arc.h>

static void client_on_free(handler_t *tcp_handler) {
    arc_ctx_t *arc = handler_custom_data(tcp_handler);
    client_ctx_t *ctx = arc_ctx_get(arc);
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_lock(&ctx->mtx);
#endif

    ctx->tcp = NULL;

    if (ctx->rd) {
        handler_t *rd = (handler_t *) ctx->rd;
        handler_unregister(rd);
    }

#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&ctx->mtx);
#endif

    arc_ctx_free(arc);
}

static void client_on_rd_free(handler_t *rd) {
    arc_ctx_t *arc = handler_custom_data(rd);
    client_ctx_t *ctx = arc_ctx_get(arc);
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_lock(&ctx->mtx);
#endif

    ctx->rd = NULL;

#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&ctx->mtx);
#endif

    arc_ctx_free(arc);
}

typedef struct {
    arc_ctx_t *ctx;
    loop_t *loop;
} client_cache_ctx_t;

typedef struct {
    // the slice actually points to `buf`
    slice_t slice;
    bool eof;
    char buf[CACHE_BUFFER_SIZE];
} cache_buf_t;

static char const bad_request[] =
    "HTTP/1.1 400 Bad Request\r\n"
    SERVER_HEADER
    "Connection: close\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 17\r\n"
    "\r\n"
    "400 Bad Request\r\n";
static char const method_not_allowed[] =
    "HTTP/1.1 405 Method Not Allowed\r\n"
    SERVER_HEADER
    "Connection: close\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 24\r\n"
    "\r\n"
    "405 Method Not Allowed\r\n";

static slice_t const bad_request_slice = {
    .base = bad_request,
    .len = sizeof(bad_request),
};
static slice_t const method_not_allowed_slice = {
    .base = method_not_allowed,
    .len = sizeof(method_not_allowed),
};

static error_t *client_on_req_err_write(
    loop_t *,
    tcp_handler_t *handler,
    size_t slice_count,
    slice_t const[static slice_count]
) {
    handler_unregister((handler_t *) handler);

    return NULL;
}

static error_t *client_cache_on_write(
    loop_t *,
    tcp_handler_t *handler,
    size_t slice_count,
    slice_t const slices[static slice_count]
) {
    assert(slice_count == 1);

    // `slices` here is actually a pointer to the `slice` field of `cache_buf_t`.
    // Thus we derive the pointer to the whole allocation.
    // The pointers here point to the same allocation, so if you're a fan of pointer provenance (and
    // you better be!), it's all totally legal.
    // *And* we can cast away the constness because the pointer we initially obtained was not const
    // to begin with.
    cache_buf_t *buf = (cache_buf_t *)((char *) slices - offsetof(cache_buf_t, slice));

    if (buf->eof) {
        // the shutdown should not matter, I think, except for having the client notified of the EOF
        // earlier, so it's not a bad thing either
        log_printf(LOG_DEBUG, "Closing the connection");
        tcp_shutdown_output(handler);
        handler_unregister((handler_t *) handler);
    }

    log_printf(LOG_DEBUG, "Freeing the buffer in on_write: %p", (void *) buf);
    free(buf);

    return NULL;
}

static error_t *client_cache_on_write_error(
    loop_t *,
    tcp_handler_t *,
    error_t *err,
    size_t slice_count,
    slice_t const slices[static slice_count],
    size_t
) {
    assert(slice_count == 1);

    // see the comment in `client_cache_on_write`
    cache_buf_t *buf = (cache_buf_t *)((char *) slices - offsetof(cache_buf_t, slice));
    log_printf(LOG_DEBUG, "Freeing the buffer in on_error: %p", (void *) buf);
    free(buf);

    // the tcp handler's generic error handler will free everything
    return err;
}

static error_t *client_cache_on_read(cache_rd_t *rd, loop_t *) {
    error_t *err = NULL;

    arc_ctx_t *arc = handler_custom_data((handler_t *) rd);
    client_ctx_t *ctx = arc_ctx_get(arc);
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_lock(&ctx->mtx);
#endif

    if (ctx->tcp == NULL) {
#ifndef WAXY_PTHREADS_DISABLED
        assert_mutex_unlock(&ctx->mtx);
#endif

        // the TCP client has been unregistered
        handler_unregister((handler_t *) rd);

        return err;
    }

    cache_buf_t *buf = malloc(sizeof(cache_buf_t));
    err = error_wrap("Could not allocate a buffer", OK_IF(buf != NULL));
    if (err) goto malloc_fail;

    log_printf(LOG_DEBUG, "Allocated %p", (void *) buf);
    buf->slice = (slice_t) {
        .base = buf->buf,
        .len = 0,
    };
    buf->eof = false;

    size_t count = cache_rd_read(rd, buf->buf, CACHE_BUFFER_SIZE, &buf->eof);
    buf->slice.len = count;

    handler_lock((handler_t *) ctx->tcp);
    err = tcp_write(ctx->tcp, 1, &buf->slice,
        client_cache_on_write, client_cache_on_write_error);
    handler_unlock((handler_t *) ctx->tcp);
    if (err) goto write_fail;

#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&ctx->mtx);
#endif

    return err;

write_fail:
    free(buf);

malloc_fail:
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&ctx->mtx);
#endif

    // we're going to fail, so wrap up the party
    handler_unregister((handler_t *) ctx->tcp);

    // failing here would abort the whole program
    // that we must not do, for this is but a mere client, one of the many of its kind
    error_log_free(&err, LOG_ERR, ERROR_VERBOSITY_BACKTRACE | ERROR_VERBOSITY_SOURCE_CHAIN);

    return err;
}

static error_t *client_cache_on_update(cache_rd_t *rd, loop_t *, cache_entry_state_t state) {
    error_t *err = NULL;

    arc_ctx_t *arc = handler_custom_data((handler_t *) rd);
    client_ctx_t *ctx = arc_ctx_get(arc);
#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_lock(&ctx->mtx);
#endif

    if (ctx->tcp == NULL) {
#ifndef WAXY_PTHREADS_DISABLED
        assert_mutex_unlock(&ctx->mtx);
#endif
        handler_unregister((handler_t *) rd);

        return err;
    }

    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    tcp_remote_info(ctx->tcp, ip, &port);

#ifndef WAXY_PTHREADS_DISABLED
    assert_mutex_unlock(&ctx->mtx);
#endif

    switch (state) {
    case CACHE_ENTRY_INVALID:
        log_printf(
            LOG_WARN,
            "The cache entry the client %s:%u was reading from has been invalidated; dropping the connection",
            ip, port
        );
        handler_unregister((handler_t *) ctx->tcp);

        return err;

    case CACHE_ENTRY_PARTIAL:
        // not that this can ever happen
        return err;

    case CACHE_ENTRY_COMPLETE:
        log_printf(LOG_DEBUG, "Download complete for %s:%u", ip, port);
        // if we get this event, we'll (or, actually, we do already) have the read callback invoked
        // with eof set to true, so we don't need to do any special handling
        return err;
    }

    abort();
}

static error_t *client_launch_cache_rd(client_cache_ctx_t *cache_ctx, cache_rd_t *rd) {
    error_t *err = NULL;

    arc_ctx_t *arc = arc_ctx_share(cache_ctx->ctx);
    client_ctx_t *ctx = arc_ctx_get(arc);

    loop_t *loop = cache_ctx->loop;

    ctx->rd = rd;

    handler_set_custom_data((handler_t *) rd, arc);
    handler_set_on_free((handler_t *) rd, client_on_rd_free);
    cache_rd_set_on_read(rd, client_cache_on_read);
    cache_rd_set_on_update(rd, client_cache_on_update);
    err = loop_register(loop, (handler_t *) rd);

    return err;
}

static error_t *client_on_cache_hit(void *data, cache_rd_t *rd) {
    error_t *err = NULL;

    client_cache_ctx_t *cache_ctx = data;
    err = client_launch_cache_rd(cache_ctx, rd);
    if (err) goto fail;

    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    tcp_remote_info(arc_ctx_get(cache_ctx->ctx)->tcp, ip, &port);
    log_printf(LOG_DEBUG, "Fetched an entry from the cache for %s:%u", ip, port);

fail:
    return err;
}

static error_t *client_on_cache_miss(void *data, cache_rd_t *rd, cache_wr_t *wr) {
    error_t *err = NULL;

    bool rd_owned = true;
    client_cache_ctx_t *cache_ctx = data;
    err = upstream_init(wr, cache_ctx->loop);
    if (err) goto upstream_init_fail;

    err = client_launch_cache_rd(cache_ctx, rd);
    if (err) goto rd_launch_fail;
    rd_owned = false;

    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    tcp_remote_info(arc_ctx_get(cache_ctx->ctx)->tcp, ip, &port);
    log_printf(
        LOG_DEBUG,
        "The resource the client %s:%u has requested was not present in the cache",
        ip, port
    );

    return err;

rd_launch_fail:
    // we don't really care if the upstream handler continues, actually

upstream_init_fail:
    if (rd_owned) {
        handler_free((handler_t *) rd);
    }

    return err;
}

static error_t *client_process_request(
    arc_ctx_t *arc,
    loop_t *loop,
    tcp_handler_t *handler,
    slice_t method,
    slice_t path,
    int minor_version,
    bool *unregister
) {
    error_t *err = NULL;
    *unregister = true;

    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    tcp_remote_info(handler, ip, &port);

    bool url_owned = false;

    if (minor_version != 0 && minor_version != 1) {
        log_printf(
            LOG_WARN,
            "A client %s:%u has sent a request with an unsupported HTTP version 1.%d",
            ip, port,
            minor_version
        );

        goto fail;
    }

    if (slice_cmp(method, slice_from_cstr("GET")) != 0) {
        log_printf(
            LOG_WARN,
            "A client %s:%u has sent a request with an unsupported method %.*s",
            ip, port,
            (int) method.len, method.base
        );
        err = error_wrap("Could not sent an error response",
            tcp_write(handler, 1, &method_not_allowed_slice, client_on_req_err_write, NULL));

        if (err) goto fail;
    }

    url_t url = {0};
    bool fatal = false;
    err = url_parse(path, &url, &fatal);
    url_owned = !fatal;

    if (!err) {
        err = error_wrap("Unsupported scheme", OK_IF(
            slice_cmp(url.scheme, slice_from_cstr("http")) == 0));
    }

    if (!err) {
        err = error_wrap("The hostname is not specified", OK_IF(!url.host_null));
    }

    if (!err) {
        if (url.port_null) {
            url.port = 80;
            url.port_null = false;
        }
    }

    if (err) {
        log_printf(LOG_WARN, "A client %s:%u has sent an invalid URL", ip, port);
        error_log_free(&err, LOG_WARN, ERROR_VERBOSITY_SHORT);

        goto fail;
    }

    client_cache_ctx_t cache_ctx = {
        .ctx = arc,
        .loop = loop,
    };
    err = cache_fetch(arc_ctx_get(arc)->cache, &url, client_on_cache_hit, client_on_cache_miss, &cache_ctx);
    if (err) goto fail;

    *unregister = false;

fail:
    if (url_owned) {
        string_free(&url.buf);
    }

    return err;
}

static error_t *client_handle_http_request(
    arc_ctx_t *arc,
    loop_t *loop,
    tcp_handler_t *handler,
    size_t prev_len,
    bool eof
) {
    error_t *err = NULL;

    client_ctx_t *ctx = arc_ctx_get(arc);
    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    tcp_remote_info(handler, ip, &port);
    bool unregister = false;

    if (string_len(&ctx->buf) > MAX_REQUEST_SIZE) {
        log_printf(
            LOG_WARN,
            "A client %s:%u has sent a request of %zu bytes, which is a bit overboard",
            ip,
            port,
            string_len(&ctx->buf)
        );
        unregister = true;

        goto fail;
    }

    slice_t method = slice_empty();
    slice_t path = slice_empty();
    int minor_version = 0;
    size_t num_headers = MAX_HEADERS;
    int count = phr_parse_request(
        string_as_cptr(&ctx->buf),
        string_len(&ctx->buf),
        &method.base,
        &method.len,
        &path.base,
        &path.len,
        &minor_version,
        ctx->headers,
        &num_headers,
        prev_len
    );

    if (count == -2) {
        if (eof) {
            log_printf(LOG_WARN, "A client %s:%u has sent a truncated request", ip, port);
            unregister = true;

            goto fail;
        }

        // partial data
        return err;
    } else if (count == -1) {
        // failure
        log_printf(LOG_WARN, "A client %s:%u has sent an invalid request", ip, port);
        err = error_wrap("Could not send a bad request response",
            tcp_write(handler, 1, &bad_request_slice, client_on_req_err_write, NULL));

        if (err) {
            error_log_free(&err, LOG_ERR, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE);
            unregister = true;
        }

        goto fail;
    }

    assert(count >= 0);
    tcp_read(handler, NULL, NULL);
    log_printf(LOG_DEBUG, "shut down input");
    tcp_shutdown_input(handler);
    err = client_process_request(
        arc, loop, handler,
        method, path,
        minor_version,
        &unregister
    );
    if (err) goto fail;

fail:
    if (unregister) {
        handler_unregister((handler_t *) handler);
    }

    return err;
}

static error_t *client_on_error(loop_t *, tcp_handler_t *handler, error_t *err) {
    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    tcp_remote_info(handler, ip, &port);
    log_printf(LOG_ERR, "An error has occured while processing a client %s:%u", ip, port);
    error_log_free(&err, LOG_ERR, ERROR_VERBOSITY_BACKTRACE | ERROR_VERBOSITY_SOURCE_CHAIN);
    handler_unregister((handler_t *) handler);

    return err;
}

static error_t *client_on_read(loop_t *loop, tcp_handler_t *handler, slice_t slice) {
    error_t *err = NULL;

    arc_ctx_t *arc = handler_custom_data((handler_t *) handler);
    client_ctx_t *ctx = arc_ctx_get(arc);

    size_t prev_len = string_len(&ctx->buf);

    err = error_wrap("Could not append read data to the buffer", error_from_common(
        string_append_slice(&ctx->buf, slice.base, slice.len)));
    if (err) goto append_fail;

    err = client_handle_http_request(arc, loop, handler, prev_len, tcp_is_eof(handler));
    if (err) goto handle_fail;

handle_fail:
append_fail:
    return err;
}

error_t *client_init(tcp_handler_t *handler, cache_t *cache) {
    error_t *err = NULL;

    client_ctx_t *ctx = calloc(1, sizeof(client_ctx_t));
    err = error_wrap("Could not allocate the context", OK_IF(ctx != NULL));
    if (err) goto ctx_calloc_fail;

    ctx->cache = cache;
    ctx->headers = calloc(MAX_HEADERS, sizeof(struct phr_header));
    err = error_wrap("Could not allocate the context", OK_IF(ctx->headers != NULL));
    if (err) goto header_calloc_fail;

    err = error_wrap("Could not allocate a buffer", error_from_common(string_new(&ctx->buf)));
    if (err) goto string_new_fail;

    ctx->tcp = handler;
    ctx->rd = NULL;

#ifndef WAXY_PTHREADS_DISABLED
    pthread_mutexattr_t mtx_attr;
    err = error_from_errno(pthread_mutexattr_init(&mtx_attr));
    if (err) goto mtx_init_fail;

    pthread_mutexattr_settype(&mtx_attr, PTHREAD_MUTEX_ERRORCHECK);
    err = error_from_errno(pthread_mutex_init(&ctx->mtx, &mtx_attr));
    pthread_mutexattr_destroy(&mtx_attr);
    if (err) goto mtx_init_fail;
#endif

    arc_ctx_t *arc = arc_ctx_new(ctx);
    err = OK_IF(arc != NULL);
    if (err) goto arc_new_fail;

    handler_set_custom_data((handler_t *) handler, arc);
    handler_set_on_free((handler_t *) handler, (handler_on_free_cb_t) client_on_free);
    tcp_set_on_error(handler, client_on_error);
    tcp_read(handler, client_on_read, NULL);

    return err;

arc_new_fail:
#ifndef WAXY_PTHREADS_DISABLED
    pthread_mutex_destroy(&ctx->mtx);

mtx_init_fail:
#endif

string_new_fail:
    free(ctx->headers);

header_calloc_fail:
    free(ctx);

ctx_calloc_fail:
    return err;
}
