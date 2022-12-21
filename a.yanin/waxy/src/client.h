#pragma once

#include <common/loop/tcp.h>

#include "cache.h"

error_t *client_init(tcp_handler_t *handler, cache_t *cache);
void client_free_ctx(tcp_handler_t *handler);
