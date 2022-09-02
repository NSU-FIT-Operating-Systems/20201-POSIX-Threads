#pragma once

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common/posix/error.h"

posix_err_t wrapper_fork(pid_t *result);
posix_err_t wrapper_waitpid(pid_t pid, int *wstatus, int options, pid_t *result);
posix_err_t wrapper_waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);
posix_err_t wrapper_execvp(char const *file, char *const argv[]);
