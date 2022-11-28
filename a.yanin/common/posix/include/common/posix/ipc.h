#pragma once

#include <stdio.h>
#include <unistd.h>

#include "common/posix/error.h"

posix_err_t wrapper_pipe(int *rd_fd, int *wd_fd);
posix_err_t wrapper_popen(char const *command, char const *type, FILE **stream);
posix_err_t wrapper_pclose(FILE *stream, int *exit_code);
