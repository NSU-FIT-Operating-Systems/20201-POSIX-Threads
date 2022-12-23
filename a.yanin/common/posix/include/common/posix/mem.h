#pragma once

#include <sys/mman.h>

#include "common/posix/error.h"

posix_err_t wrapper_mmap(
    void *addr,
    size_t length,
    int prot,
    int flags,
    int fd,
    off_t offset,
    void **result
);
posix_err_t wrapper_munmap(void *addr, size_t length);
