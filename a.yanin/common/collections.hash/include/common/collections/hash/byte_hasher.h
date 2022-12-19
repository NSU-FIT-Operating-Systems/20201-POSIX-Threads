#pragma once

#include <stddef.h>

typedef struct {
    size_t seed;
    size_t input_size;
} byte_hasher_config_t;

size_t byte_hasher(void const *value, byte_hasher_config_t const *cfg);
size_t byte_hasher_secondary(void const *value, byte_hasher_config_t const *cfg);
