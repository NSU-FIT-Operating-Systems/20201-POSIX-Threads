#include "common/collections/hash/byte_hasher.h"

#include <assert.h>

size_t byte_hasher(void const *value, byte_hasher_config_t const *config) {
    assert(value != NULL);
    assert(config != NULL);
    assert(config->input_size != 0);

    unsigned char const *bytes = (unsigned char const *) value;
    size_t hash = config->seed;

    for (size_t i = 0; i < config->input_size; ++i) {
        unsigned char c = bytes[i];

        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return hash;
}

size_t byte_hasher_secondary(void const *value, byte_hasher_config_t const *config) {
    assert(value != NULL);
    assert(config != NULL);
    assert(config->input_size != 0);

    size_t hash = byte_hasher(value, config);

    if (hash % 2 == 0) {
        ++hash;
    }

    return hash;
}
