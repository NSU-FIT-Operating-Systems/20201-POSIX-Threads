#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/posix/error.h"

posix_err_t wrapper_fcntli(int fd, int cmd, int arg);
posix_err_t wrapper_fstat(int fd, struct stat *statbuf);
posix_err_t wrapper_lstat(char const *restrict path, struct stat *statbuf);
posix_err_t wrapper_unlink(char const *path);
