#pragma once

#include <signal.h>

#include "common/posix/error.h"

posix_err_t wrapper_sigprocmask(int how, sigset_t const *restrict set, sigset_t *restrict oldset);
posix_err_t wrapper_sigwait(sigset_t const *restrict set, int *restrict sig);
