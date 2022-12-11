#pragma once

#include <stddef.h>

typedef struct {
    void *base;
    size_t len;
} slice_t;
