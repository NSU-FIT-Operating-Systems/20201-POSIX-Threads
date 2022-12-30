#pragma once

#include <common/error.h>
#include <common/posix/error.h>

error_t *error_from_posix(posix_err_t err);
