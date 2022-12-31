#pragma once

#include <stdbool.h>

struct stack;

struct copy_info {
    char *src_path;
    char *dst_path;
    bool is_dir;
    struct stack *stack;
};

// Note that is_dir must be set outside for efficiency
struct copy_info *create_copy_info(const struct copy_info *prev, 
                                   const char *filename);
