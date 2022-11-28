#include "common/posix/file.h"

#include <assert.h>

posix_err_t wrapper_fcntli(int fd, int cmd, int arg) {
    assert(fd >= 0);

    errno = 0;

    if (fcntl(fd, cmd, arg) < 0) {
        return make_posix_err("fcntl(2) failed");
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_fstat(int fd, struct stat *statbuf) {
    assert(fd >= 0);
    assert(statbuf != NULL);

    errno = 0;

    if (fstat(fd, statbuf) < 0) {
        return make_posix_err("fstat(2) failed");
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_lstat(char const *restrict path, struct stat *statbuf) {
    assert(path != NULL);
    assert(statbuf != NULL);

    errno = 0;

    if (lstat(path, statbuf) < 0) {
        return make_posix_err("lstat(2) failed");
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_unlink(char const *path) {
    assert(path != NULL);

    errno = 0;

    if (unlink(path) < 0) {
        return make_posix_err("unlink(2) failed");
    }

    return make_posix_err_ok();
}
