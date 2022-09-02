#pragma once

#include <time.h>

#include "common/posix/error.h"

posix_err_t wrapper_nanosleep(struct timespec *const time);
