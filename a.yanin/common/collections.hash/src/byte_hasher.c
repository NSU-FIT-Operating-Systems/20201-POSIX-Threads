#include "common/collections/hash/byte_hasher.h"

#include <assert.h>

struct byte_hasher_state {
    size_t value;
};

void byte_hasher_digest_u8(byte_hasher_state_t *state, uint8_t value) {
    state->value *= 33;
    state->value += value;
}

void byte_hasher_digest_u16(byte_hasher_state_t *state, uint16_t value) {
    byte_hasher_digest_u8(state, value >> 8);
    byte_hasher_digest_u8(state, value);
}

void byte_hasher_digest_u32(byte_hasher_state_t *state, uint32_t value) {
    byte_hasher_digest_u16(state, value >> 16);
    byte_hasher_digest_u16(state, value);
}

void byte_hasher_digest_u64(byte_hasher_state_t *state, uint64_t value) {
    byte_hasher_digest_u32(state, value >> 32);
    byte_hasher_digest_u32(state, value);
}

void byte_hasher_digest_slice(byte_hasher_state_t *state, const char *start, size_t len) {
    byte_hasher_digest_u64(state, len);

    for (size_t i = 0; i < len; ++i) {
        byte_hasher_digest_u8(state, start[i]);
    }
}

void byte_hasher_digest_bool(byte_hasher_state_t *state, bool value) {
    byte_hasher_digest_u8(state, value ? 1 : 0);
}

size_t byte_hasher(void const *value, byte_hasher_config_t const *config) {
    assert(value != NULL);
    assert(config != NULL);
    assert(config->hash != NULL);

    byte_hasher_state_t state = { .value = config->seed };
    config->hash(value, &state);

    return state.value;
}

size_t byte_hasher_secondary(void const *value, byte_hasher_config_t const *config) {
    assert(value != NULL);
    assert(config != NULL);
    assert(config->hash != NULL);

    size_t hash = byte_hasher(value, config);

    if (hash % 2 == 0) {
        ++hash;
    }

    return hash;
}
