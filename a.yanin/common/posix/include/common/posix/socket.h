#pragma once

#include <sys/socket.h>

#include "common/posix/error.h"

posix_err_t wrapper_socket(int domain, int type, int protocol, int *fd);
posix_err_t wrapper_bind(int sockfd, struct sockaddr const *addr, socklen_t addrlen);
posix_err_t wrapper_connect(int sockfd, struct sockaddr const *addr, socklen_t addrlen);
posix_err_t wrapper_listen(int sockfd, int backlog);
posix_err_t wrapper_accept(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen,
    int *fd);
posix_err_t wrapper_shutdown(int sockfd, int how);
posix_err_t wrapper_getsockopt(int sockfd, int level, int optname, void *restrict optval,
    socklen_t *restrict optlen);
posix_err_t wrapper_setsockopt(int sockfd, int level, int optname, void const *optval,
    socklen_t optlen);
