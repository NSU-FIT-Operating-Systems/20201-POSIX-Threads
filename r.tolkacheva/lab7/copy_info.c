#include "copy_info.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct copy_info *create_copy_info(const struct copy_info *prev, 
                                   const char *filename) {
    assert(prev != NULL);
    assert(filename != NULL);
    assert(prev->is_dir);

    // Allocate memory for the new copy_info struct
    struct copy_info *new_info = malloc(sizeof(struct copy_info));
    if (new_info == NULL) {
        // Error allocating memory
        return NULL;
    }

    const size_t filename_len = strlen(filename);

    // Construct the source and destination paths
    const size_t src_len = strlen(prev->src_path) + filename_len + 2;
    new_info->src_path = malloc(src_len);
    if (new_info->src_path == NULL) {
        goto FREE_INFO;
    }
    snprintf(new_info->src_path, src_len, "%s/%s", prev->src_path, filename);

    const size_t dst_len = strlen(prev->dst_path) + filename_len + 2;
    new_info->dst_path = malloc(dst_len);
    if (new_info->dst_path == NULL) {
        goto FREE_SRC;
    }
    snprintf(new_info->dst_path, dst_len, "%s/%s", prev->dst_path, filename);

    // Return the new copy_info struct
    return new_info;

    // Error handling
    FREE_SRC:
    free(new_info->src_path);
    FREE_INFO:
    free(new_info);

    return NULL;
}
