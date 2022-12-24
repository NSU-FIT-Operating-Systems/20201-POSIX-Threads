#include "upstream.h"

#include <assert.h>
#include <stdlib.h>

#include <netdb.h>

#include <picohttpparser/picohttpparser.h>

#include <common/error-codes/adapter.h>
#include <common/loop/tcp.h>

#include "gai-adapter.h"
#include "util.h"

enum {
    UPSTREAM_MAX_HEADERS = 512,
};

typedef struct {
    string_t buf;
    struct phr_header *headers;
    struct addrinfo *addr_head;
    struct addrinfo *addr_next;
    cache_wr_t *wr;
    tcp_handler_t *tcp;
    bool response_parsed;
} upstream_ctx_t;

// -1 is to account for the NUL terminator
#define SLICE_INIT_FROM_CSTR(STR) { \
        .base = STR, \
        .len = sizeof(STR) - 1, \
    }

static slice_t const request_slices[] = {
    SLICE_INIT_FROM_CSTR("GET /"),
    SLICE_INIT_FROM_CSTR(" HTTP/1.1\r\n"
        "User-Agent: waxy/101\r\n"
        "Connection: close\r\n"
        "Host: "
    ),
    SLICE_INIT_FROM_CSTR("\r\n"
        "Accept: */*\r\n"
        "\r\n"
    )
};

static void upstream_ctx_free(handler_t *data) {
    upstream_ctx_t *ctx = handler_custom_data(data);

    if (ctx == NULL) {
        return;
    }

    // freeing the string twice is ok
    string_free(&ctx->buf);
    // the headers are set to `NULL` after they are freed
    free(ctx->headers);

    if (ctx->addr_head != NULL) {
        freeaddrinfo(ctx->addr_head);
    }

    cache_wr_free(ctx->wr);
    free(ctx);
}

static error_t *upstream_on_error(
    loop_t *,
    tcp_handler_t *handler,
    error_t *err
) {
    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    tcp_remote_info(handler, ip, &port);

    error_log_free(&err, LOG_ERR, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE);
    log_printf(LOG_ERR, "Handling the upstream %s:%u failed", ip, port);

    handler_unregister((handler_t *) handler);

    return err;
}

static error_t *upstream_on_read(loop_t *, tcp_handler_t *handler, slice_t slice) {
    error_t *err = NULL;

    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    tcp_remote_info(handler, ip, &port);

    upstream_ctx_t *ctx = handler_custom_data((handler_t *) handler);

    if (!ctx->response_parsed) {
        size_t last_len = string_len(&ctx->buf);

        err = error_from_common(string_append_slice(&ctx->buf, slice.base, slice.len));
        if (err) goto unregister;

        int minor_version = -1;
        int status = -1;
        slice_t msg = slice_empty();
        size_t num_headers = UPSTREAM_MAX_HEADERS;
        int count = phr_parse_response(
            string_as_cptr(&ctx->buf), string_len(&ctx->buf),
            &minor_version,
            &status,
            &msg.base, &msg.len,
            ctx->headers,
            &num_headers,
            last_len
        );

        if (count == -2) {
            // partial data
            if (tcp_is_eof(handler)) {
                log_printf(LOG_ERR, "The upstream %s:%u has sent an abruptly ended response",
                    ip, port);

                goto unregister;
            }
        } else if (count == -1) {
            log_printf(LOG_ERR, "The upstream %s:%u has sent an invalid HTTP response", ip, port);

            goto unregister;
        } else {
            if (status == 200) {
                error_t *commit_err = cache_wr_commit(ctx->wr);

                if (commit_err) {
                    error_log_free(
                        &commit_err,
                        LOG_ERR,
                        ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE
                    );
                    log_printf(
                        LOG_ERR,
                        "Could not commit a response from the upstream %s:%u to the cache",
                        ip, port
                    );
                } else {
                    log_printf(
                        LOG_INFO,
                        "Received a 200 status code, committing the response to the cache"
                    );
                }
            } else {
                log_printf(
                    LOG_INFO,
                    "Received a %d status code: not committing to the cache",
                    status
                );
            }

            string_free(&ctx->buf);
            free(ctx->headers);
            ctx->headers = NULL;
            ctx->response_parsed = true;
        }
    }

    log_printf(LOG_DEBUG, "Written %zu bytes to the cache entry", slice.len);
    err = cache_wr_write(ctx->wr, slice);
    if (err) goto unregister;

    if (tcp_is_eof(handler)) {
        cache_wr_complete(ctx->wr);

        goto unregister;
    }

    return err;

unregister:
    handler_unregister((handler_t *) handler);

    return err;
}

static error_t *upstream_on_request_write(
    loop_t *,
    tcp_handler_t *handler,
    size_t slice_count,
    slice_t const slices[static slice_count]
) {
    // there's nothing we have left to tell the upstream
    tcp_shutdown_output(handler);

    free((slice_t *) slices);

    tcp_read(handler, upstream_on_read, NULL);

    return NULL;
}

static error_t *upstream_on_request_write_err(
    loop_t *,
    tcp_handler_t *,
    error_t *err,
    size_t slice_count,
    slice_t const slices[static slice_count],
    size_t
) {
    assert(slice_count == 1);
    free((slice_t *) slices);

    return err;
}

static error_t *upstream_on_connect(loop_t *, tcp_handler_t *handler) {
    error_t *err = NULL;

    upstream_ctx_t *ctx = handler_custom_data((handler_t *) handler);
    freeaddrinfo(ctx->addr_head);
    ctx->addr_head = NULL;
    ctx->addr_next = NULL;

    url_t const *url = cache_wr_url(ctx->wr);

    slice_t *slices = calloc(5, sizeof(slice_t));
    err = OK_IF(slices != NULL);
    if (err) goto calloc_fail;

    slices[0] = request_slices[0];
    slices[1] = url->path;
    slices[2] = request_slices[1];
    slices[3] = url->host;
    slices[4] = request_slices[2];

    err = tcp_write(handler, 5, slices, upstream_on_request_write, upstream_on_request_write_err);
    if (err) goto tcp_write_fail;

    return err;

tcp_write_fail:
    free(slices);

calloc_fail:
    return err;
}

static error_t *upstream_reconnect(loop_t *loop, tcp_handler_t *handler, error_t *err);

static error_t *upstream_new_handler(upstream_ctx_t *ctx, tcp_handler_t **result) {
    error_t *err = NULL;

    err = error_wrap("Could not connect to the upstream server", OK_IF(ctx->addr_next != NULL));
    if (err) goto addr_next_fail;

    struct addrinfo *addr = ctx->addr_next;
    ctx->addr_next = addr->ai_next;

    tcp_handler_t *tcp = NULL;
    err = tcp_connect(
        addr->ai_addr, addr->ai_addrlen,
        upstream_on_connect, upstream_reconnect,
        &tcp
    );
    if (err) goto connect_fail;

    tcp_set_on_error(tcp, upstream_on_error);
    handler_set_custom_data((handler_t *) tcp, ctx);
    handler_set_on_free((handler_t *) tcp, upstream_ctx_free);

    *result = tcp;

    return err;

connect_fail:
addr_next_fail:
    return err;
}

static error_t *upstream_reconnect(loop_t *loop, tcp_handler_t *handler, error_t *err) {
    upstream_ctx_t *ctx = handler_custom_data((handler_t *) handler);
    tcp_handler_t *tcp = NULL;
    error_t *reconnect_err = upstream_new_handler(ctx, &tcp);

    if (reconnect_err) {
        err = error_combine(reconnect_err, err);

        goto fail;
    }

    error_log_free(&err, LOG_DEBUG, ERROR_VERBOSITY_SOURCE_CHAIN);
    handler_set_custom_data((handler_t *) handler, NULL);
    handler_unregister((handler_t *) handler);

    err = loop_register(loop, (handler_t *) tcp);
    if (err) goto fail;

    ctx->tcp = tcp;

fail:
    return err;
}

error_t *upstream_init(cache_wr_t *wr, loop_t *loop) {
    error_t *err = NULL;

    url_t const *url = cache_wr_url(wr);
    assert(!url->host_null);
    assert(!url->port_null);

    bool host_owned = false;
    bool port_owned = false;

    string_t host;
    err = error_from_common(string_from_slice(url->host.base, url->host.len, &host));
    if (err) goto host_fail;
    host_owned = true;

    string_t port;
    err = error_from_common(string_sprintf(&port, "%u", url->port));
    if (err) goto port_fail;
    port_owned = false;

    upstream_ctx_t *ctx = calloc(1, sizeof(upstream_ctx_t));
    err = error_wrap("Could not allocate memory for the upstream handler", OK_IF(ctx != NULL));
    if (err) goto ctx_calloc_fail;

    err = error_wrap("Could not allocate an input buffer", error_from_common(
        string_new(&ctx->buf)));
    if (err) goto string_new_fail;

    ctx->headers = calloc(UPSTREAM_MAX_HEADERS, sizeof(struct phr_header));
    err = error_wrap("Could not allocate memory for the upstream handler",
        OK_IF(ctx->headers != NULL));
    if (err) goto header_calloc_fail;

    struct addrinfo *addr_head = NULL;
    err = error_wrap("Could not resolve the upstream URL", error_from_gai(getaddrinfo(
        string_as_cptr(&host),
        string_as_cptr(&port),
        &(struct addrinfo) {
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM,
            .ai_protocol = IPPROTO_TCP,
            .ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV,
        },
        &addr_head
    )));
    if (err) goto gai_fail;

    ctx->addr_head = addr_head;
    ctx->addr_next = addr_head;

    string_free(&port);
    string_free(&host);
    host_owned = false;
    port_owned = false;

    tcp_handler_t *tcp = NULL;
    err = upstream_new_handler(ctx, &tcp);
    if (err) goto new_handler_fail;

    ctx->wr = wr;
    ctx->tcp = tcp;
    ctx->response_parsed = false;

    err = error_wrap("Could not register the upstream handler",
        loop_register(loop, (handler_t *) tcp));
    if (err) goto register_fail;

    return err;

register_fail:
    handler_free((handler_t *) tcp);

new_handler_fail:
    freeaddrinfo(addr_head);

gai_fail:
    free(ctx->headers);

header_calloc_fail:
    string_free(&ctx->buf);

string_new_fail:
    free(ctx);

ctx_calloc_fail:
    if (port_owned) {
        string_free(&port);
    }

port_fail:
    if (host_owned) {
        string_free(&host);
    }

host_fail:
    cache_wr_free(wr);

    return err;
}
