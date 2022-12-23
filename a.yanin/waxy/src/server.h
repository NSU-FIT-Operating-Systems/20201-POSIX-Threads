#pragma once

#include <common/loop/loop.h>
#include <common/loop/tcp.h>

#include "cache.h"
#include "executor.h"

typedef struct server_ctx server_ctx_t;

// Listens on a socket for client connections.
typedef struct server {
    loop_t *loop;
    executor_t *executor;
    cache_t *cache;
    server_ctx_t *ctx;
} server_t;

error_t *server_new(char const *port, size_t cache_size, server_t *result);
void server_free(server_t *self);
void server_stop(server_t *self);
error_t *server_run(server_t *self);
