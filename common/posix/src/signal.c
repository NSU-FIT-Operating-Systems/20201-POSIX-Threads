#include "common/posix/signal.h"

#include <assert.h>

posix_err_t wrapper_sigprocmask(int how, sigset_t const *restrict set, sigset_t *restrict oldset) {
    if (sigprocmask(how, set, oldset) < 0) {
        return make_posix_err("sigprocmask(2) failed");
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_sigwait(sigset_t const *restrict set, int *restrict sig) {
    assert(set != NULL);
    assert(sig != NULL);

    int err = sigwait(set, sig);

    if (err != 0) {
        return (posix_err_t) {
            .errno_code = err,
            .message = "sigwait(3) failed",
        };
    }

    return make_posix_err_ok();
}
