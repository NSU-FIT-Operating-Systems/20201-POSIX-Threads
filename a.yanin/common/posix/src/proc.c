#include "common/posix/proc.h"

#include <assert.h>

posix_err_t wrapper_fork(pid_t *result) {
    assert(result != NULL);

    errno = 0;
    pid_t pid = fork();

    if (pid < 0) {
        return make_posix_err("fork(2) failed");
    }

    *result = pid;

    return make_posix_err_ok();
}

posix_err_t wrapper_waitpid(pid_t pid, int *wstatus, int options, pid_t *result) {
    pid_t return_value = -1;

    do {
        errno = 0;
        return_value = waitpid(pid, wstatus, options);
    } while (return_value < 0 && errno == EINTR);

    if (return_value < 0) {
        return make_posix_err("waitpid(2) failed");
    }

    if (result != NULL) {
        *result = return_value;
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options) {
    int return_value = -1;

    do {
        errno = 0;
    } while ((return_value = waitid(idtype, id, infop, options) < 0) && errno == EINTR);

    if (return_value < 0) {
        return make_posix_err("waitid(2) failed");
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_execvp(char const *file, char *const argv[]) {
    assert(file != NULL);
    assert(argv != NULL);

    execvp(file, argv);

    return make_posix_err("execvp(3) failed");
}

posix_err_t wrapper_sysconf(int name, long *result) {
    assert(result != NULL);

    errno = 0;
    long retval = sysconf(name);

    if (errno != 0) {
        return make_posix_err("sysconf(3) failed");
    }

    *result = retval;

    return make_posix_err_ok();
}
