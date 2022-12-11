#include "common/loop/tcp.h"

#include <common/posix/adapter.h>
#include <common/posix/file.h>
#include <netinet/in.h>

#include "common/loop/loop.h"
#include "util.h"

typedef struct {
    slice_t const *slices;
    size_t slice_count;
    tcp_on_write_cb_t on_write;
    tcp_on_read_error_cb_t on_error;
} write_req_t;

#define VEC_ELEMENT_TYPE write_req_t
#define VEC_LABEL wrreq
#define VEC_CONFIG (COLLECTION_DECLARE | COLLECTION_DEFINE | COLLECTION_STATIC)
#include <common/collections/vec.h>

typedef enum {
    // connect(2) returned EINPROGRESS
    TCP_HANDLER_CONNECTING,
    // connect(2) succeeded immediately, but the on_connect callback was not yet called
    TCP_HANDLER_CONNECTED,
    // connect(2) has failed immediately, but the on_error callback was not yet called
    TCP_HANDLER_FAIL,
    // ready
    TCP_HANDLER_ESTABLISHED,
} tcp_handler_state_t;

struct tcp_handler_server {
    handler_t handler;
    tcp_server_on_new_conn_cb_t on_new_conn;
    tcp_server_on_listen_error_cb_t on_listen_error;
    tcp_server_on_error_cb_t on_error;
};

struct tcp_handler {
    handler_t handler;
    vec_wrreq_t write_reqs;
    tcp_on_error_cb_t on_error;
    union {
        struct {
            tcp_on_read_cb_t on_read;
            tcp_on_read_error_cb_t on_read_error;
        };

        struct {
            tcp_on_connect_cb_t on_connect;
            tcp_on_connect_error_cb_t on_connect_error;
        };
    };
    struct {
        socklen_t len;

        union {
            char buf[sizeof(struct sockaddr_in6)];
            struct sockaddr addr;
        };
    } peer_address;
    error_t *pending_error;
    void *custom_data;
    tcp_handler_state_t state;
    bool input_shut;
    bool output_shut;
    bool eof;
};

static error_t *get_socket_error(int fd) {
    int err_code = 0;
    error_t *err = error_from_posix(
        wrapper_getsockopt(fd, SOL_SOCKET, SO_ERROR, &err_code, &(socklen_t) { sizeof(int) })
    );
    if (err) return err;

    err = error_wrap("A socket has signalled an error", error_from_errno(err_code));

    return err;
}

static void tcp_handler_free(handler_t *self) {
    error_t *err = error_wrap("Could not close the socket",
        error_from_posix(wrapper_close(handler_fd(self))));

    if (err) {
        error_log_free(&err, LOG_WARN, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE);
    }
}

static void tcp_server_free(tcp_handler_server_t *self) {
    tcp_handler_free(&self->handler);
}

static error_t *tcp_server_process(tcp_handler_server_t *self, loop_t *loop, poll_flags_t flags) {
    error_t *err = NULL;

    if (flags & LOOP_ERR) {
        err = get_socket_error(handler_fd(&self->handler));
    }

    if (err) goto fail;

    if (flags & LOOP_READ) {
        err = self->on_new_conn(loop, self);
    }

fail:
    return err;
}

static error_t *tcp_server_on_error(tcp_handler_server_t *self, loop_t *loop, error_t *err) {
    if (self->on_error) {
        err = self->on_error(loop, self, err);
    }

    return err;
}

static handler_vtable_t const tcp_server_vtable = {
    .free = (handler_vtable_free_t) tcp_server_free,
    .process = (handler_vtable_process_t) tcp_server_process,
    .on_error = (handler_vtable_on_error_t) tcp_server_on_error,
};

error_t *tcp_server_new(struct sockaddr *addr, socklen_t addrlen, tcp_handler_server_t **result) {
    assert(addr->sa_family == AF_INET || addr->sa_family == AF_INET6);

    error_t *err = NULL;

    tcp_handler_server_t *self = malloc(sizeof(tcp_handler_server_t));
    err = error_wrap("Could not allocate memory for the handler", OK_IF(self != NULL));
    if (err) goto malloc_fail;

    int fd = -1;
    err = error_from_posix(wrapper_socket(addr->sa_family, SOCK_STREAM, 0, &fd));
    if (err) goto socket_fail;

    err = error_from_posix(wrapper_bind(fd, addr, addrlen));
    if (err) goto bind_fail;

    err = error_wrap("Could not switch the socket to non-blocking mode",
        error_from_posix(wrapper_fcntli(fd, F_SETFL, O_NONBLOCK)));
    if (err) goto fcntl_fail;

    handler_init(&self->handler, &tcp_server_vtable, fd);
    self->on_new_conn = NULL;
    self->on_listen_error = NULL;
    self->on_error = NULL;
    *handler_pending_mask(&self->handler) = LOOP_READ;

    *result = self;

    return err;

fcntl_fail:
bind_fail:
    err = error_combine(err, error_from_posix(wrapper_close(fd)));

socket_fail:
    free(self);

malloc_fail:
    return err;
}

static void client_init(tcp_handler_t *self, int fd);

error_t *tcp_accept(tcp_handler_server_t *self, tcp_handler_t **result) {
    error_t *err = NULL;

    tcp_handler_t *client = calloc(1, sizeof(tcp_handler_t));
    err = error_wrap("Could not allocate memory for the client handler", OK_IF(client != NULL));
    if (err) goto malloc_fail;

    client->peer_address.len = sizeof(client->peer_address.buf);

    int fd = -1;
    err = error_from_posix(wrapper_accept(
        handler_fd(&self->handler),
        &client->peer_address.addr,
        &client->peer_address.len,
        &fd
    ));
    if (err) goto accept_fail;

    client_init(client, fd);
    client->state = TCP_HANDLER_ESTABLISHED;
    *result = client;

    return err;

accept_fail:
    free(client);

malloc_fail:
    return err;
}

static void tcp_client_free(tcp_handler_t *self) {
    tcp_handler_free(&self->handler);
}

static error_t *tcp_client_handle_connect_fail(tcp_handler_t *self, loop_t *loop, error_t *err) {
    assert(self->state == TCP_HANDLER_FAIL);

    *handler_pending_mask(&self->handler) = 0;

    if (self->on_connect_error != NULL) {
        err = self->on_connect_error(loop, self, err);
    }

    return err;
}

static error_t *tcp_client_handle_connected(tcp_handler_t *self, loop_t *loop, poll_flags_t flags) {
    assert(self->state == TCP_HANDLER_CONNECTED);

    error_t *err = NULL;
    *handler_pending_mask(&self->handler) = 0;

    if (flags & LOOP_ERR) {
        err = get_socket_error(handler_fd(&self->handler));
    }

    if (err) {
        self->state = TCP_HANDLER_FAIL;

        return tcp_client_handle_connect_fail(self, loop, err);
    }

    self->state = TCP_HANDLER_ESTABLISHED;
    err = self->on_connect(loop, self);

    return err;
}

static error_t *tcp_client_handle_connecting(
    tcp_handler_t *self,
    loop_t *loop,
    poll_flags_t flags
) {
    assert(self->state == TCP_HANDLER_CONNECTING);

    error_t *err = NULL;

    if (flags & LOOP_ERR) {
        err = get_socket_error(handler_fd(&self->handler));
    }

    if (err) {
        self->state = TCP_HANDLER_FAIL;

        return tcp_client_handle_connect_fail(self, loop, err);
    }

    if (flags & LOOP_WRITE) {
        self->state = TCP_HANDLER_CONNECTED;

        return tcp_client_handle_connected(self, loop, flags);
    }

    return err;
}

static error_t *tcp_client_handle_read(tcp_handler_t *self, loop_t *loop) {
    TODO("handle LOOP_READ");
}

static error_t *tcp_client_handle_write(tcp_handler_t *self, loop_t *loop) {
    TODO("handle LOOP_WRITE");
}

static error_t *tcp_client_handle_established(
    tcp_handler_t *self,
    loop_t *loop,
    poll_flags_t flags
) {
    assert(self->state == TCP_HANDLER_ESTABLISHED);

    error_t *err = NULL;

    if (flags & LOOP_ERR) {
        err = get_socket_error(handler_fd(&self->handler));
    }

    if (err && self->on_read_error != NULL) {
        err = self->on_read_error(loop, self, err);
    }

    if (err) return err;

    if (flags & LOOP_HUP) {
        self->input_shut = true;
    }

    if (self->on_read != NULL && flags & LOOP_READ) {
        err = tcp_client_handle_read(self, loop);
        if (err) return err;
    }

    if (vec_wrreq_len(&self->write_reqs) != 0 && flags & LOOP_WRITE) {
        err = tcp_client_handle_write(self, loop);
        if (err) return err;
    }

    return err;
}

static error_t *tcp_client_process(tcp_handler_t *self, loop_t *loop, poll_flags_t flags) {
    switch (self->state) {
    case TCP_HANDLER_CONNECTING:
        return tcp_client_handle_connecting(self, loop, flags);

    case TCP_HANDLER_CONNECTED:
        return tcp_client_handle_connected(self, loop, flags);

    case TCP_HANDLER_FAIL: {
        error_t *err = self->pending_error;
        self->pending_error = NULL;

        return tcp_client_handle_connect_fail(self, loop, err);
    }

    case TCP_HANDLER_ESTABLISHED:
        return tcp_client_handle_established(self, loop, flags);
    }
}

static error_t *tcp_client_on_error(tcp_handler_t *self, loop_t *loop, error_t *err) {
    if (self->on_error) {
        err = self->on_error(loop, self, err);
    }

    return err;
}

static handler_vtable_t const tcp_client_vtable = {
    .free = (handler_vtable_free_t) tcp_client_free,
    .process = (handler_vtable_process_t) tcp_client_process,
    .on_error = (handler_vtable_on_error_t) tcp_client_on_error,
};

static void client_init(tcp_handler_t *self, int fd) {
    handler_init(&self->handler, &tcp_client_vtable, fd);
    self->write_reqs = vec_wrreq_new();
    self->on_error = NULL;
    self->on_connect = NULL;
    self->on_connect_error = NULL;
    self->pending_error = NULL;
    self->custom_data = NULL;
    self->state = TCP_HANDLER_CONNECTING;
    self->input_shut = false;
    self->output_shut = false;
    self->eof = false;
}

error_t *tcp_connect(
    struct sockaddr const *addr,
    socklen_t addrlen,
    tcp_on_connect_cb_t on_connect,
    tcp_on_connect_error_cb_t on_error,
    tcp_handler_t **result
) {
    assert(addr->sa_family == AF_INET || addr->sa_family == AF_INET6);

    error_t *err = NULL;

    tcp_handler_t *self = calloc(1, sizeof(tcp_handler_t));
    err = error_wrap("Could not allocate memory for the handler", OK_IF(self != NULL));
    if (err) goto malloc_fail;

    int fd = -1;
    err = error_from_posix(wrapper_socket(addr->sa_family, SOCK_STREAM, 0, &fd));
    if (err) goto socket_fail;

    err = error_wrap("Could not switch the socket to non-blocking mode",
        error_from_posix(wrapper_fcntli(fd, F_SETFL, O_NONBLOCK)));
    if (err) goto fcntl_fail;

    tcp_handler_state_t state = TCP_HANDLER_CONNECTING;

    {
        posix_err_t status;
        err = error_from_posix(status = wrapper_connect(fd, addr, addrlen));

        if (status.errno_code == EINPROGRESS) {
            error_free(&err);
            state = TCP_HANDLER_CONNECTING;
        } else if (err) {
            self->pending_error = err;
            err = NULL;
            state = TCP_HANDLER_FAIL;
        } else {
            state = TCP_HANDLER_CONNECTED;
        }
    }

    if (state == TCP_HANDLER_CONNECTING) {
        *handler_pending_mask(&self->handler) = LOOP_WRITE;
    } else {
        handler_force(&self->handler);
    }

    self->on_connect = on_connect;
    self->on_connect_error = on_error;

    self->peer_address.len = addrlen;
    memcpy(&self->peer_address.buf[0], addr, addrlen);

    *result = self;

    return err;

fcntl_fail:
    err = error_combine(err, error_from_posix(wrapper_close(fd)));

socket_fail:
    free(self);

malloc_fail:
    return err;
}
