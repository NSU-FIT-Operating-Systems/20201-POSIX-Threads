#pragma once

#include "error.h"

err_t http_get(char const *url, int *sock_fd);
