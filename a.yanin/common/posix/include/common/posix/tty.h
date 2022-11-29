#pragma once

#include <termios.h>
#include <unistd.h>

#include "common/posix/error.h"

posix_err_t wrapper_tcgetattr(int fd, struct termios *settings);
posix_err_t wrapper_tcsetattr(int fd, int optional_actions, struct termios *settings);
