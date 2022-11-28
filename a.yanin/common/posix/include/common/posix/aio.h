#pragma once

#include <aio.h>

#include "common/posix/error.h"

posix_err_t wrapper_lio_listio(int mode, struct aiocb *const list[restrict], int nent,
    struct sigevent *restrict sig);
posix_err_t wrapper_aio_read(struct aiocb *aiocbp);
posix_err_t wrapper_aio_error(struct aiocb const *aiocbp);
posix_err_t wrapper_aio_return(struct aiocb *aiocbp, ssize_t *result);
posix_err_t wrapper_aio_suspend(struct aiocb const *const list[], int nent,
    struct timespec const *timeout);
