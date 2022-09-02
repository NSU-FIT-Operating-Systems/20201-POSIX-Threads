#pragma once

#include <sys/types.h>
#include <dirent.h>

#include "common/posix/error.h"

posix_err_t wrapper_opendir(char const *name, DIR **result);
posix_err_t wrapper_fdopendir(int fd, DIR **result);
posix_err_t wrapper_readdir(DIR *dir, struct dirent **result);
posix_err_t wrapper_closedir(DIR *dir);
