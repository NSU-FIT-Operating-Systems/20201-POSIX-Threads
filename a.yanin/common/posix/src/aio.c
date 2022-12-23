#include "common/posix/aio.h"

#include <assert.h>

posix_err_t wrapper_lio_listio(
    int mode,
    struct aiocb *const list[restrict],
    int nent,
    struct sigevent *restrict sig
) {
    assert(list != NULL);
    assert(nent >= 0);

    errno = 0;

    if (lio_listio(mode, list, nent, sig) < 0) {
        return make_posix_err("lio_listio(3) failed");
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_aio_read(struct aiocb *aiocbp) {
    assert(aiocbp != NULL);

    errno = 0;

    if (aio_read(aiocbp) < 0) {
        return make_posix_err("aio_read(3) failed");
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_aio_error(struct aiocb const *aiocbp) {
    assert(aiocbp != NULL);

    errno = 0;
    int result = aio_error(aiocbp);

    if (result < 0) {
        return make_posix_err("aio_error(3) failed");
    }

    if (result != 0) {
        return (posix_err_t) {
            .errno_code = result,
            .message = "aio_error(3) indicated an error condition",
        };
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_aio_return(struct aiocb *aiocbp, ssize_t *result) {
    assert(aiocbp != NULL);
    assert(result != NULL);

    errno = 0;
    ssize_t return_value = aio_return(aiocbp);

    if (return_value < 0) {
        return make_posix_err("aio_return(3) failed");
    }

    *result = return_value;

    return make_posix_err_ok();
}

posix_err_t wrapper_aio_suspend(
    struct aiocb const *const list[],
    int nent,
    struct timespec const *timeout
) {
    assert(list != NULL);
    assert(nent >= 0);

    int status = -1;

    do {
        errno = 0;
        status = aio_suspend(list, nent, timeout);
    } while (status < 0 && errno == EINTR);

    if (status < 0) {
        return make_posix_err("aio_suspend(3) failed");
    }

    return make_posix_err_ok();
}
