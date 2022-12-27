#pragma once

#include <stdbool.h>
#include <stdint.h>

[[maybe_unused]]
static bool is_be_arch(void) {
    uint32_t native = 0x01020304;
    char *bytes = (char *) &native;

    return bytes[0] == 0x01;
}

[[maybe_unused]]
static uint16_t u16_to_be(uint16_t host) {
    if (is_be_arch()) {
        return host;
    }

    unsigned char *bytes = (unsigned char *) &host;

    return ((uint16_t) bytes[1] << 8) | (uint16_t) bytes[0];
}

[[maybe_unused]]
static uint32_t u32_to_be(uint32_t host) {
    if (is_be_arch()) {
        return host;
    }

    unsigned char *bytes = (unsigned char *) &host;

    return ((uint32_t) bytes[3] << 24) |
        ((uint32_t) bytes[2] << 16) |
        ((uint32_t) bytes[1] << 8) |
        (uint32_t) bytes[0];
}

[[maybe_unused]]
static uint64_t u64_to_be(uint64_t host) {
    if (is_be_arch()) {
        return host;
    }

    unsigned char *bytes = (unsigned char *) &host;

    return ((uint64_t) bytes[7] << 56) |
        ((uint64_t) bytes[6] << 48) |
        ((uint64_t) bytes[5] << 40) |
        ((uint64_t) bytes[4] << 32) |
        ((uint64_t) bytes[3] << 24) |
        ((uint64_t) bytes[2] << 16) |
        ((uint64_t) bytes[1] << 8) |
        (uint64_t) bytes[0];
}

// these functions are involutive
[[maybe_unused]]
static uint16_t u16_from_be(uint16_t be) {
    return u16_to_be(be);
}

[[maybe_unused]]
static uint32_t u32_from_be(uint32_t be) {
    return u32_to_be(be);
}

[[maybe_unused]]
static uint64_t u64_from_be(uint64_t be) {
    return u64_to_be(be);
}
