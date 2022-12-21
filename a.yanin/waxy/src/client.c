#include "client.h"

#include <assert.h>
#include <stdlib.h>

#include <picohttpparser/picohttpparser.h>

#include <common/error-codes/adapter.h>

#include "cache.h"
#include "util.h"

#define SERVER_HEADER "Server: waxy\r\n"

enum {
    MAX_HEADERS = 512,
    // have you ever seen an HTTP GET request larger than 16 MiB? me neither.
    MAX_REQUEST_SIZE = 16 * 1024 * 1024,
};

typedef struct client_ctx {
    cache_t *cache;
    string_t buf;
    struct phr_header *headers;
} client_ctx_t;

static char const bad_request_response[] =
    "HTTP/1.1 400 Bad Request\r\n"
    SERVER_HEADER
    "Connection: close\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 17\r\n"
    "\r\n"
    "400 Bad Request\r\n";

static slice_t bad_request_response_slice = {
    .base = bad_request_response,
    .len = sizeof(bad_request_response),
};

error_t *client_on_bad_req_write(loop_t *loop, tcp_handler_t *handler) {
    loop_unregister(loop, (handler_t *) handler);
    client_free_ctx(handler);

    return NULL;
}

error_t *client_process_request(
    client_ctx_t *ctx,
    loop_t *loop,
    tcp_handler_t *handler,
    slice_t method,
    slice_t path,
    int minor_version,
    size_t num_headers
) {
    TODO("process the request");
}

static error_t *client_handle_http_request(
    client_ctx_t *ctx,
    loop_t *loop,
    tcp_handler_t *handler,
    size_t prev_len,
    bool eof
) {
    error_t *err = NULL;

    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    tcp_remote_info(handler, ip, &port);

    if (string_len(&ctx->buf) > MAX_REQUEST_SIZE) {
        log_printf(
            LOG_WARN,
            "A client %s:%u has sent a request of %zu bytes, which is a bit overboard",
            ip,
            port,
            string_len(&ctx->buf)
        );
        loop_unregister(loop, (handler_t *) handler);
        client_free_ctx(handler);

        return err;
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

            goto fail;
        }

        // partial data
        return err;
    } else if (count == -1) {
        // failure
        log_printf(LOG_WARN, "A client %s:%u has sent an invalid request", ip, port);
        err = error_wrap("Could not send a bad request response",
            tcp_write(handler, 1, &bad_request_response_slice, client_on_bad_req_write, NULL));

        if (err) {
            error_log_free(&err, LOG_ERR, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE);

            goto fail;
        }

        return err;
    }

    assert(count >= 0);
    err = client_process_request(ctx, loop, handler, method, path, minor_version, num_headers);
    if (err) goto fail;

    string_remove_slice(&ctx->buf, 0, count);

    if (tcp_is_eof(handler)) {
        log_printf(LOG_DEBUG, "A client %s:%u has closed the connections", ip, port);
    }

    return err;

fail:
    loop_unregister(loop, (handler_t *) handler);
    client_free_ctx(handler);

    return err;
}

static error_t *client_on_error(loop_t *loop, tcp_handler_t *handler, error_t *err) {
    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    tcp_remote_info(handler, ip, &port);
    log_printf(LOG_ERR, "An error has occured while processing a client %s:%u", ip, port);
    error_log_free(&err, LOG_ERR, ERROR_VERBOSITY_BACKTRACE | ERROR_VERBOSITY_SOURCE_CHAIN);
    loop_unregister(loop, (handler_t *) handler);
    client_free_ctx(handler);

    return err;
}

static error_t *client_on_read(loop_t *loop, tcp_handler_t *handler, slice_t slice) {
    error_t *err = NULL;

    client_ctx_t *ctx = handler_custom_data((handler_t *) handler);

    size_t prev_len = string_len(&ctx->buf);

    err = error_wrap("Could not append read data to the buffer", error_from_common(
        string_append_slice(&ctx->buf, slice.base, slice.len)));
    if (err) goto append_fail;

    err = client_handle_http_request(ctx, loop, handler, prev_len, tcp_is_eof(handler));
    if (err) goto handle_fail;
    // XXX: `ctx` must not be used here because it may have been freed by the call above

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

    handler_set_custom_data((handler_t *) handler, ctx);
    tcp_set_on_error(handler, client_on_error);
    tcp_read(handler, client_on_read, NULL);

    return err;

string_new_fail:
    free(ctx->headers);

header_calloc_fail:
    free(ctx);

ctx_calloc_fail:
    return err;
}

// TODO: better to hook this to the handler's free method
void client_free_ctx(tcp_handler_t *handler) {
    client_ctx_t *ctx = handler_custom_data((handler_t *) handler);
    string_free(&ctx->buf);
    free(ctx);
}
