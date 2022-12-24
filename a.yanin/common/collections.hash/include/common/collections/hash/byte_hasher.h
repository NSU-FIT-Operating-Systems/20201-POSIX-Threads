#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct byte_hasher_state byte_hasher_state_t;

typedef struct {
    size_t seed;
    void (*hash)(void const *value, byte_hasher_state_t *state);
} byte_hasher_config_t;

size_t byte_hasher(void const *value, byte_hasher_config_t const *cfg);
size_t byte_hasher_secondary(void const *value, byte_hasher_config_t const *cfg);

void byte_hasher_digest_u8(byte_hasher_state_t *state, uint8_t value);
void byte_hasher_digest_u16(byte_hasher_state_t *state, uint16_t value);
void byte_hasher_digest_u32(byte_hasher_state_t *state, uint32_t value);
void byte_hasher_digest_u64(byte_hasher_state_t *state, uint64_t value);
void byte_hasher_digest_slice(byte_hasher_state_t *state, char const *start, size_t len);
void byte_hasher_digest_bool(byte_hasher_state_t *state, bool value);
