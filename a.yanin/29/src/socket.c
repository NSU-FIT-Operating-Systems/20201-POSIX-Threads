#include "socket.h"

#include <assert.h>

#include <arpa/inet.h>
#include <netdb.h>

#include <common/log/log.h>
#include <common/posix/io.h>
#include <common/posix/socket.h>

err_t create_socket_and_connect(char const *hostname, char const *port, int *sock_fd) {
    assert(hostname != NULL);
    assert(port != NULL);
    assert(sock_fd != NULL);

    err_t error = OK;

    struct addrinfo *addr_head;
    error = ERR((err_gai_t) getaddrinfo(hostname, port, &(struct addrinfo) {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
        .ai_flags = AI_ADDRCONFIG,
    }, &addr_head), "failed to resolve host");

    if (ERR_FAILED(error)) goto gai_fail;

    int fd = -1;

    for (struct addrinfo *addr = addr_head; addr != NULL; addr = addr->ai_next, fd = -1) {
        struct sockaddr_in *addr_in = (struct sockaddr_in *) addr->ai_addr;
        char const *hostname = inet_ntoa(addr_in->sin_addr);
        uint16_t port = ntohs(addr_in->sin_port);
        log_printf(LOG_INFO, "Connecting to %s:%d", hostname, port);

        err_t open_error = ERR(
            wrapper_socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol, &fd),
            "could not create a remote socket"
        );

        if (ERR_FAILED(open_error)) {
            err_log_free(LOG_WARN, &open_error);

            continue;
        }

        open_error = ERR(wrapper_connect(fd, addr->ai_addr, addr->ai_addrlen), "connection failed");

        if (!ERR_FAILED(open_error)) {
            break;
        }

        err_log_free(LOG_WARN, &open_error);
        err_t close_error = ERR(wrapper_close(fd), "could not close the failed socket");

        if (ERR_FAILED(close_error)) {
            err_log_free(LOG_WARN, &close_error);
        }
    }

    freeaddrinfo(addr_head);
    error = ERR((bool)(fd != -1), "failed to connect to a remote host");

    if (ERR_FAILED(error)) {
        goto connect_fail;
    }

    *sock_fd = fd;

connect_fail:
gai_fail:
    return error;
}

err_t create_socket_and_listen(char const *port, int backlog, int *sock_fd) {
    assert(backlog > 0);
    assert(sock_fd != NULL);

    err_t error = OK;

    struct addrinfo *addr_head;
    error = ERR((err_gai_t) getaddrinfo(NULL, port, &(struct addrinfo) {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
        .ai_flags = AI_ADDRCONFIG | AI_PASSIVE,
    }, &addr_head), "failed to resolve bind address");

    if (ERR_FAILED(error)) goto gai_fail;

    int fd = -1;

    for (struct addrinfo *addr = addr_head; addr != NULL; addr = addr->ai_next, fd = -1) {
        struct sockaddr_in *addr_in = (struct sockaddr_in *) addr->ai_addr;
        char const *hostname = inet_ntoa(addr_in->sin_addr);
        uint16_t port = ntohs(addr_in->sin_port);
        log_printf(LOG_INFO, "Trying to listen on %s:%d...", hostname, port);

        err_t open_error = ERR(
            wrapper_socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol, &fd),
            "could not create a server socket"
        );

        if (ERR_FAILED(open_error)) {
            err_log_free(LOG_WARN, &open_error);

            continue;
        }

        open_error = ERR(wrapper_bind(fd, addr->ai_addr, addr->ai_addrlen), "binding failed");

        if (!ERR_FAILED(open_error)) {
            break;
        }

        err_log_free(LOG_WARN, &open_error);
        err_t close_error = ERR(wrapper_close(fd), "could not close the failed socket");

        if (ERR_FAILED(close_error)) {
            err_log_free(LOG_WARN, &close_error);
        }
    }

    freeaddrinfo(addr_head);
    error = ERR((bool)(fd != -1), "failed to bind to any address");

    if (ERR_FAILED(error)) {
        goto bind_fail;
    }

    error = ERR(wrapper_listen(fd, backlog), "failed to mark the socket as listening");

    if (ERR_FAILED(error)) {
        goto listen_fail;
    }

    *sock_fd = fd;

    return error;

listen_fail:
    {
        err_t warn = ERR(wrapper_close(fd), "failed to close the socket");

        if (ERR_FAILED(warn)) {
            err_log_free(LOG_WARN, &warn);
        }
    }

bind_fail:
gai_fail:
    return error;
}

void shutdown_and_close(int sock_fd) {
    err_t warn = ERR(wrapper_shutdown(sock_fd, SHUT_RDWR), "failed to shutdown a socket");

    if (ERR_FAILED(warn)) {
        err_log_free(LOG_WARN, &warn);
    }

    warn = ERR(wrapper_close(sock_fd), "failed to close a socket");

    if (ERR_FAILED(warn)) {
        err_log_free(LOG_WARN, &warn);
    }
}

err_t get_pending_socket_error(int sock_fd) {
    assert(sock_fd >= 0);

    err_t error = OK;

    int code = 0;
    error = ERR(wrapper_getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &code,
        &(socklen_t) {sizeof(int)}), "failed to retrieve an error from the socket");

    if (!ERR_FAILED(error)) {
        error = ERR((err_errno_t) code, "the socket received an error");
    }

    return error;
}

