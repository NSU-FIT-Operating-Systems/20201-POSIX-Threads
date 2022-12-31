#include "common/posix/io.h"

#include <assert.h>

posix_err_t wrapper_open(char const *path, int flags, int *result) {
    assert(path != NULL);
    assert(result != NULL);

    int fd = -1;

    do {
        errno = 0;
        fd = open(path, flags);
    } while (fd < 0 && errno == EINTR);

    if (fd < 0) {
        return make_posix_err("open(2) failed");
    }

    *result = fd;

    return make_posix_err_ok();
}

posix_err_t wrapper_openat(int fd, char const *path, int flags, int *result) {
    assert(fd >= 0 || fd == AT_FDCWD);
    assert(path != NULL);
    assert(result != NULL);

    int result_fd = -1;

    do {
        errno = 0;
        result_fd = openat(fd, path, flags);
    } while (result_fd < 0 && errno == EINTR);

    if (result_fd < 0) {
        return make_posix_err("openat(2) failed");
    }

    *result = result_fd;

    return make_posix_err_ok();
}

posix_err_t wrapper_close(int fd) {
    assert(fd >= 0);

    errno = 0;

    if (close(fd) < 0) {
        return make_posix_err("close(2) failed");
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_read(int fd, void *buf, size_t count, ssize_t *result) {
    assert(fd >= 0);
    assert(buf != NULL);

    ssize_t return_value = -1;

    do {
        errno = 0;
        return_value = read(fd, buf, count);
    } while (return_value < 0 && errno == EINTR);

    if (return_value < 0) {
        return make_posix_err("read(2) failed");
    }

    if (result != NULL) {
        *result = return_value;
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_write(int fd, void const *buf, size_t count, ssize_t *result) {
    assert(fd >= 0);
    assert(buf != NULL);

    ssize_t return_value = -1;

    do {
        errno = 0;
        return_value = write(fd, buf, count);
    } while (return_value < 0 && errno == EINTR);

    if (return_value < 0) {
        return make_posix_err("write(2) failed");
    }

    if (result != NULL) {
        *result = return_value;
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_lseek(int fd, off_t offset, int whence, off_t *result) {
    assert(fd >= 0);

    errno = 0;
    off_t return_value = lseek(fd, offset, whence);

    if (return_value < 0) {
        return make_posix_err("lseek(2) failed");
    }

    if (result != NULL) {
        *result = return_value;
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_fileno(FILE *file, int *result) {
    assert(file != NULL);
    assert(result != NULL);

    errno = 0;
    int fd = fileno(file);

    if (fd < 0) {
        return make_posix_err("fileno(3) failed");
    }

    *result = fd;

    return make_posix_err_ok();
}

posix_err_t wrapper_dup(int oldfd, int *result) {
    assert(oldfd >= 0);
    assert(result != NULL);

    int fd = -1;

    do {
        errno = 0;
        fd = dup(oldfd);
    } while (fd < 0 && errno == EINTR);

    if (fd < 0) {
        return make_posix_err("dup(2) failed");
    }

    *result = fd;

    return make_posix_err_ok();
}

posix_err_t wrapper_select(
    int nfds,
    fd_set *restrict readfds,
    fd_set *restrict writefds,
    fd_set *restrict expectfds,
    struct timeval *restrict timeout,
    int *result
) {
    assert(result != NULL);

    errno = 0;
    int return_value = select(nfds, readfds, writefds, expectfds, timeout);

    if (return_value < 0) {
        return make_posix_err("select(2) failed");
    }

    *result = return_value;

    return make_posix_err_ok();
}

posix_err_t wrapper_poll(struct pollfd *fds, nfds_t nfds, int timeout, int *result) {
    assert(result != NULL);

    int return_value;

    do {
        errno = 0;
        return_value = poll(fds, nfds, timeout);
    } while (return_value < 0 && errno == EINTR);

    if (return_value < 0) {
        return make_posix_err("poll(2) failed");
    }

    *result = return_value;

    return make_posix_err_ok();
}

posix_err_t wrapper_writev(int fd, struct iovec const *iov, int iovcnt, ssize_t *result) {
    assert(result != NULL);
    assert(fd >= 0);
    assert(iovcnt >= 0);

    ssize_t return_value = -1;

    do {
        errno = 0;
        return_value = writev(fd, iov, iovcnt);
    } while (return_value < 0 && errno == EINTR);

    if (return_value < 0) {
        return make_posix_err("writev(2) failed");
    }

    *result = return_value;

    return make_posix_err_ok();
}
