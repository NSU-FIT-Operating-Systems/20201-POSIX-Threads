#include "common/posix/tty.h"

#include <assert.h>

posix_err_t wrapper_tcgetattr(int fd, struct termios *settings) {
    assert(fd >= 0);
    assert(settings != NULL);

    errno = 0;

    if (tcgetattr(fd, settings) < 0) {
        return make_posix_err("tcgetattr(2) failed");
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_tcsetattr(int fd, int optional_actions, struct termios *settings) {
    assert(fd >= 0);
    assert(settings != NULL);

    errno = 0;

    if (tcsetattr(fd, optional_actions, settings) < 0) {
        return make_posix_err("tcsetattr(2) failed");
    }

    return make_posix_err_ok();
}
