#include "common/posix/socket.h"

#include <assert.h>

posix_err_t wrapper_socket(int domain, int type, int protocol, int *fd) {
    assert(fd != NULL);

    errno = 0;
    int result = socket(domain, type, protocol);

    if (result < 0) {
        return make_posix_err("socket(2) failed");
    }

    *fd = result;

    return make_posix_err_ok();
}

posix_err_t wrapper_bind(int sockfd, struct sockaddr const *addr, socklen_t addrlen) {
    assert(addr != NULL);

    errno = 0;

    if (bind(sockfd, addr, addrlen) < 0) {
        return make_posix_err("bind(2) failed");
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_connect(int sockfd, struct sockaddr const *addr, socklen_t addrlen) {
    assert(addr != NULL);

    errno = 0;

    if (connect(sockfd, addr, addrlen) < 0) {
        return make_posix_err("connect(2) failed");
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_listen(int sockfd, int backlog) {
    errno = 0;

    if (listen(sockfd, backlog) < 0) {
        return make_posix_err("listen(2) failed");
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_accept(
    int sockfd,
    struct sockaddr *restrict addr,
    socklen_t *restrict addrlen,
    int *fd
) {
    assert(fd != NULL);

    errno = 0;
    int result = accept(sockfd, addr, addrlen);

    if (result < 0) {
        return make_posix_err("accept(2) failed");
    }

    *fd = result;

    return make_posix_err_ok();
}

posix_err_t wrapper_shutdown(int sockfd, int how) {
    errno = 0;

    if (shutdown(sockfd, how) < 0) {
        return make_posix_err("shutdown(2) failed");
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_getsockopt(
    int sockfd,
    int level,
    int optname,
    void *restrict optval,
    socklen_t *restrict optlen
) {
    errno = 0;

    if (getsockopt(sockfd, level, optname, optval, optlen) < 0) {
        return make_posix_err("getsockopt(2) failed");
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_setsockopt(
    int sockfd,
    int level,
    int optname,
    void const *optval,
    socklen_t optlen
) {
    errno = 0;

    if (setsockopt(sockfd, level, optname, optval, optlen) < 0) {
        return make_posix_err("setsockopt(2) failed");
    }

    return make_posix_err_ok();
}
