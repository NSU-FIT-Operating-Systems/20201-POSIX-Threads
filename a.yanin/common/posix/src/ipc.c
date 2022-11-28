#include "common/posix/ipc.h"

#include <assert.h>
#include <stddef.h>

posix_err_t wrapper_pipe(int *rd_fd, int *wd_fd) {
    assert(rd_fd != NULL);
    assert(wd_fd != NULL);

    errno = 0;
    int pipefd[2];

    if (pipe(pipefd) < 0) {
        return make_posix_err("pipe(2) failed");
    }

    *rd_fd = pipefd[0];
    *wd_fd = pipefd[1];

    return make_posix_err_ok();
}

posix_err_t wrapper_popen(char const *command, char const *type, FILE **stream) {
    assert(command != NULL);
    assert(type != NULL);
    assert(stream != NULL);

    errno = 0;
    FILE *result = popen(command, type);

    if (result == NULL) {
        return make_posix_err("popen(3) failed");
    }

    *stream = result;

    return make_posix_err_ok();
}

posix_err_t wrapper_pclose(FILE *stream, int *exit_code) {
    assert(stream != NULL);

    errno = 0;
    int result = pclose(stream);

    if (result < 0) {
        return make_posix_err("pclose(3) failed");
    }

    if (exit_code != NULL) {
        *exit_code = result;
    }

    return make_posix_err_ok();
}
