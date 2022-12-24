#include "server.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>

#include "client.h"
#include "executor.h"
#include "gai-adapter.h"
#include "util.h"

enum {
    DEFAULT_BACKLOG = 1024,
};

typedef enum {
    // processing gai results
    SERVER_STATE_BIND,

    // listening for clients
    SERVER_STATE_LISTEN,
} server_state_t;

struct server_ctx {
    server_t *self;
    struct addrinfo *addr_head;
    struct addrinfo *next_addr;
    server_state_t state;
};

static error_t *server_new_tcp_serv(server_ctx_t *ctx, tcp_handler_server_t **result);

static error_t *server_on_fail(loop_t *loop, tcp_handler_server_t *serv, error_t *err) {
    server_ctx_t *ctx = handler_custom_data((handler_t const *) serv);

    switch (ctx->state) {
    case SERVER_STATE_BIND:
        (void) 0;
        tcp_handler_server_t *new_serv = NULL;
        error_t *serv_new_err = server_new_tcp_serv(ctx, &new_serv);

        if (serv_new_err) {
            err = error_combine(err, serv_new_err);
            goto fail;
        }

        handler_unregister((handler_t *) serv);
        serv_new_err = loop_register(loop, (handler_t *) new_serv);

        if (serv_new_err) {
            err = error_combine(err, serv_new_err);
            goto fail;
        }

        err = error_wrap("Binding failed, trying another address", err);
        error_log_free(&err, LOG_WARN, ERROR_VERBOSITY_SOURCE_CHAIN);

        break;

    case SERVER_STATE_LISTEN:
        goto fail;
    }

fail:
    return err;
}

static error_t *server_on_new_conn(loop_t *loop, tcp_handler_server_t *serv) {
    error_t *err = NULL;

    server_ctx_t *ctx = handler_custom_data((handler_t *) serv);
    ctx->state = SERVER_STATE_LISTEN;
    freeaddrinfo(ctx->addr_head);
    ctx->addr_head = NULL;

    tcp_handler_t *handler = NULL;
    err = error_wrap("Could not accept a connection", tcp_accept(serv, &handler));
    if (err) goto accept_fail;

    err = client_init(handler, ctx->self->cache);
    if (err) goto client_init_fail;

    err = error_wrap("Could not register a client handler",
        loop_register(loop, (handler_t *) handler));
    if (err) goto loop_register_fail;

    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    tcp_remote_info(handler, ip, &port);
    log_printf(LOG_INFO, "Accepted a new connection from %s:%u", ip, port);

    return err;

loop_register_fail:
client_init_fail:
    handler_free((handler_t *) handler);
    // this makes it so that the server keeps running if there are client issues
    error_log_free(&err, LOG_WARN, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE);

accept_fail:
    return err;
}

static error_t *server_new_tcp_serv(server_ctx_t *ctx, tcp_handler_server_t **result) {
    error_t *err = NULL;

    err = error_wrap("No suitable bind address found", OK_IF(ctx->next_addr != NULL));
    if (err) goto next_addr_fail;

    struct addrinfo *addr = ctx->next_addr;
    ctx->next_addr = addr->ai_next;

    tcp_handler_server_t *serv = NULL;
    err = tcp_server_new(addr->ai_addr, addr->ai_addrlen, &serv);
    if (err) goto serv_new_fail;

    tcp_server_set_on_error(serv, server_on_fail);
    handler_set_custom_data((handler_t *) serv, ctx);

    err = tcp_server_listen(serv, DEFAULT_BACKLOG, server_on_new_conn, NULL);
    if (err) goto listen_fail;

    *result = serv;

    return err;

listen_fail:
    handler_free((handler_t *) serv);

serv_new_fail:
next_addr_fail:
    return err;
}

error_t *server_new(char const *port, size_t cache_size, server_t *result) {
    error_t *err = NULL;

    executor_t *executor = NULL;
    err = create_default_executor(&executor);
    if (err) goto executor_new_fail;

    loop_t *loop = NULL;
    err = loop_new(executor, &loop);
    if (err) goto loop_new_fail;

    server_ctx_t *ctx = calloc(1, sizeof(server_ctx_t));
    err = error_wrap("Could not allocate memory for the server", OK_IF(ctx != NULL));
    if (err) goto ctx_calloc_fail;

    struct addrinfo *addr_head = NULL;
    err = error_from_gai(getaddrinfo(NULL, port, &(struct addrinfo) {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
        .ai_flags = AI_ADDRCONFIG | AI_PASSIVE,
    }, &addr_head));
    if (err) goto gai_fail;

    err = OK_IF(addr_head != NULL);
    if (err) goto gai_fail;

    *ctx = (server_ctx_t) {
        .self = result,
        .addr_head = addr_head,
        .next_addr = addr_head,
        .state = SERVER_STATE_BIND,
    };

    tcp_handler_server_t *serv = NULL;
    err = server_new_tcp_serv(ctx, &serv);
    if (err) goto serv_new_fail;

    cache_t *cache = NULL;
    err = cache_new(cache_size, &cache);
    if (err) goto cache_new_fail;

    err = loop_register(loop, (handler_t *) serv);
    if (err) goto loop_register_fail;

    *result = (server_t) {
        .loop = loop,
        .executor = executor,
        .cache = cache,
        .ctx = ctx,
    };

    return err;

loop_register_fail:
    cache_free(cache);

cache_new_fail:
    handler_free((handler_t *) serv);

serv_new_fail:
    freeaddrinfo(addr_head);

gai_fail:
    free(ctx);

ctx_calloc_fail:
    loop_free(loop);

loop_new_fail:
    executor_free(executor);

executor_new_fail:
    return err;
}

void server_stop(server_t *self) {
    loop_stop(self->loop);
}

void server_free(server_t *self) {
    loop_free(self->loop);
    executor_free(self->executor);
    cache_free(self->cache);

    if (self->ctx->addr_head != NULL) {
        freeaddrinfo(self->ctx->addr_head);
    }

    free(self->ctx);
}

error_t *server_run(server_t *self) {
    error_t *err = loop_run(self->loop);
    executor_shutdown(self->executor);

    return err;
}
