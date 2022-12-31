#pragma once

#include <common/error-codes/error-codes.h>
#include <common/error.h>

error_t *error_from_common(common_error_code_t code);
