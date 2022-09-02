#include "common/posix/mem.h"

#include <assert.h>

posix_err_t wrapper_mmap(
    void *addr,
    size_t length,
    int prot,
    int flags,
    int fd,
    off_t offset,
    void **result
) {
    assert(result != NULL);
    assert(length > 0);

    errno = 0;
    void *return_value = mmap(addr, length, prot, flags, fd, offset);

    if (return_value == MAP_FAILED) {
        return make_posix_err("mmap(2) failed");
    }

    *result = return_value;

    return make_posix_err_ok();
}

posix_err_t wrapper_munmap(void *addr, size_t length) {
    assert(length > 0);

    errno = 0;

    if (munmap(addr, length) < 0) {
        return make_posix_err("munmap(2) failed");
    }

    return make_posix_err_ok();
}
