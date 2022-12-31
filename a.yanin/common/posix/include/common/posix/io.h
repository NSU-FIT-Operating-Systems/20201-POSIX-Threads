#pragma once

#include <stdio.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/uio.h>
#include <unistd.h>

#include "common/posix/error.h"

posix_err_t wrapper_open(char const *path, int flags, int *result);
posix_err_t wrapper_openat(int fd, char const *path, int flags, int *result);
posix_err_t wrapper_close(int fd);
posix_err_t wrapper_read(int fd, void *buf, size_t count, ssize_t *result);
posix_err_t wrapper_write(int fd, void const *buf, size_t count, ssize_t *result);
posix_err_t wrapper_lseek(int fd, off_t offset, int whence, off_t *result);
posix_err_t wrapper_fileno(FILE *file, int *result);
posix_err_t wrapper_dup(int oldfd, int *result);
posix_err_t wrapper_select(
    int nfds,
    fd_set *restrict readfds,
    fd_set *restrict writefds,
    fd_set *restrict expectfds,
    struct timeval *restrict timeout,
    int *result
);
posix_err_t wrapper_poll(struct pollfd *fds, nfds_t nfds, int timeout, int *result);
posix_err_t wrapper_writev(int fd, struct iovec const *iov, int iovcnt, ssize_t *result);
