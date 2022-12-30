#include "common/loop/tcp.h"

#include <arpa/inet.h>
#include <stdint.h>

#include <netinet/in.h>

#include <common/error-codes/adapter.h>
#include <common/posix/adapter.h>
#include <common/posix/file.h>
#include <common/posix/io.h>
#include <common/posix/proc.h>

#include "common/loop/loop.h"
#include "io.h"
#include "util.h"

enum {
    READ_BUFFER_SIZE = 16384,
};

typedef struct {
    write_req_t write_req;
    tcp_on_write_cb_t on_write;
    tcp_on_write_error_cb_t on_error;
} tcp_write_req_t;

#define VEC_ELEMENT_TYPE tcp_write_req_t
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

    tcp_handler_server_t *self = calloc(1, sizeof(tcp_handler_server_t));
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
    handler_set_pending_mask(&self->handler, LOOP_READ);

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

static error_t *tcp_client_drop_write_reqs(tcp_handler_t *self, loop_t *loop) {
    assert(loop != NULL);

    error_t *err = NULL;

    for (size_t i = 0; i < vec_wrreq_len(&self->write_reqs); ++i) {
        tcp_write_req_t const *req = vec_wrreq_get(&self->write_reqs, i);

        if (req->on_error != NULL) {
            err = error_combine(err, req->on_error(loop, self, NULL,
                req->write_req.slice_count,
                req->write_req.slices,
                req->write_req.written_count));
        }
    }

    vec_wrreq_clear(&self->write_reqs);

    return err;
}

static void tcp_client_free(tcp_handler_t *self) {
    error_t *err = error_wrap("An error has occured while freeing a TCP handler",
        tcp_client_drop_write_reqs(self, handler_loop((handler_t *) self)));

    if (err) {
        error_log_free(&err, LOG_WARN, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE);
    }

    vec_wrreq_free(&self->write_reqs);
    tcp_handler_free(&self->handler);
}

static error_t *tcp_client_handle_connect_fail(tcp_handler_t *self, loop_t *loop, error_t *err) {
    assert(self->state == TCP_HANDLER_FAIL);

    handler_set_pending_mask(&self->handler, 0);

    if (self->on_connect_error != NULL) {
        err = self->on_connect_error(loop, self, err);
    }

    return err;
}

static error_t *tcp_client_handle_connected(tcp_handler_t *self, loop_t *loop, poll_flags_t flags) {
    assert(self->state == TCP_HANDLER_CONNECTED);

    error_t *err = NULL;
    handler_set_pending_mask(&self->handler, 0);

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
    error_t *err = NULL;

    char *buf = calloc(READ_BUFFER_SIZE, 1);
    err = error_wrap("Could not allocate a buffer for received data", OK_IF(buf != NULL));
    if (err) goto calloc_fail;

    int fd = handler_fd(&self->handler);
    ssize_t read_count = -1;
    err = error_from_posix(wrapper_read(fd, buf, READ_BUFFER_SIZE, &read_count));
    if (err) goto read_fail;

    slice_t slice = {
        .base = buf,
        .len = (size_t) read_count,
    };

    if (self->input_shut && read_count == 0) {
        log_printf(LOG_DEBUG, "self->eof = true!");
        self->eof = true;
    }

    err = self->on_read(loop, self, slice);
    if (err) goto cb_fail;

    if (self->eof) {
        poll_flags_t flags = handler_pending_mask(&self->handler);
        flags &= ~LOOP_READ;
        handler_set_pending_mask(&self->handler, flags);
    }

cb_fail:
read_fail:
    free(buf);

calloc_fail:
    if (err && self->on_read_error != NULL) {
        err = self->on_read_error(loop, self, err);
    }

    return err;
}

static write_req_t *tcp_client_process_write_req_get_req(void *self_opaque) {
    tcp_handler_t *self = self_opaque;

    return &vec_wrreq_get_mut(&self->write_reqs, 0)->write_req;
}

static error_t *tcp_client_process_write_req_on_write(
    void *self_opaque,
    loop_t *loop,
    write_req_t *req
) {
    tcp_handler_t *self = self_opaque;
    tcp_write_req_t *tcp_req = (tcp_write_req_t *) req;

    if (tcp_req->on_write != NULL) {
        return tcp_req->on_write(loop, self, req->slice_count, req->slices);
    } else {
        return NULL;
    }
}

static error_t *tcp_client_process_write_req_on_error(
    void *self_opaque,
    loop_t *loop,
    write_req_t *req,
    error_t *err
) {
    tcp_handler_t *self = self_opaque;
    tcp_write_req_t *tcp_req = (tcp_write_req_t *) req;
    log_printf(LOG_DEBUG, "Had an error: buf = %p, on_error == NULL = %d", (void *) tcp_req->write_req.slices, tcp_req->on_error == NULL);

    if (tcp_req->on_error != NULL) {
        return tcp_req->on_error(loop, self, err,
            req->slice_count, req->slices, req->written_count);
    } else {
        return err;
    }
}

static error_t *tcp_client_process_write_req(
    tcp_handler_t *self,
    loop_t *loop,
    error_t *err,
    io_process_result_t *processed
) {
    return io_process_write_req(
        self,
        loop,
        err,
        processed,
        handler_fd(&self->handler),
        tcp_client_process_write_req_get_req,
        tcp_client_process_write_req_on_write,
        tcp_client_process_write_req_on_error
    );
}

static error_t *tcp_client_handle_write(tcp_handler_t *self, loop_t *loop) {
    error_t *err = NULL;

    log_printf(LOG_DEBUG, "Have %zu reqs", vec_wrreq_len(&self->write_reqs));

    while (!self->output_shut && vec_wrreq_len(&self->write_reqs) > 0) {
        io_process_result_t processed = false;
        err = tcp_client_process_write_req(self, loop, err, &processed);

        switch (processed) {
        case IO_PROCESS_FINISHED:
            vec_wrreq_remove(&self->write_reqs, 0);
            [[fallthrough]];

        case IO_PROCESS_PARTIAL:
            if (!err) {
                goto out;
            }

            [[fallthrough]];

        case IO_PROCESS_AGAIN:
            continue;
        }

        if (processed) {
            // XXX: this makes it O(n²)
            // a better choice would be a ring buffer
            log_printf(LOG_DEBUG, "Processed %p (err = %p)", (void *) vec_wrreq_get(&self->write_reqs, 0)->write_req.slices, (void *) err);
        }
    }

out:

    log_printf(LOG_DEBUG, "Now it's %zu reqs", vec_wrreq_len(&self->write_reqs));

    if (self->output_shut) {
        err = error_combine(err, error_wrap(
            "Encountered a failure while processing output shutdown",
            tcp_client_drop_write_reqs(self, loop)
        ));
    }

    if (vec_wrreq_len(&self->write_reqs) == 0) {
        poll_flags_t flags = handler_pending_mask(&self->handler);
        flags &= ~LOOP_WRITE;
        handler_set_pending_mask(&self->handler, flags);
    }

    return err;
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
        log_printf(LOG_DEBUG, "got LOOP_HUP");
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

    log_abort("self->state is invalid (%jd)", (intmax_t) self->state);
}

static error_t *tcp_client_on_error(tcp_handler_t *self, loop_t *loop, error_t *err) {
    log_printf(LOG_DEBUG, "Handling a client error");

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

    client_init(self, fd);

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
        handler_set_pending_mask(&self->handler, LOOP_WRITE);
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

error_t *tcp_server_listen(
    tcp_handler_server_t *self,
    int backlog,
    tcp_server_on_new_conn_cb_t on_new_conn,
    tcp_server_on_listen_error_cb_t on_error
) {
    error_t *err = NULL;

    err = error_from_posix(wrapper_listen(handler_fd(&self->handler), backlog));
    if (err) goto listen_fail;

    self->on_new_conn = on_new_conn;
    self->on_listen_error = on_error;
    handler_set_pending_mask(&self->handler, LOOP_READ);

listen_fail:
    return err;
}

void tcp_read(tcp_handler_t *self, tcp_on_read_cb_t on_read, tcp_on_read_error_cb_t on_error) {
    assert(self->state == TCP_HANDLER_ESTABLISHED);

    self->on_read = on_read;
    self->on_read_error = on_error;
    poll_flags_t flags = handler_pending_mask(&self->handler);

    if (self->on_read != NULL) {
        flags |= LOOP_READ;
    } else {
        flags &= ~LOOP_READ;
    }

    handler_set_pending_mask(&self->handler, flags);
}

bool tcp_is_eof(tcp_handler_t const *self) {
    return self->eof;
}

error_t *tcp_write(
    tcp_handler_t *self,
    size_t slice_count,
    slice_t const *slices,
    tcp_on_write_cb_t on_write,
    tcp_on_write_error_cb_t on_error
) {
    error_assert(error_wrap("The handler must be registered before calling tcp_write",
        OK_IF(handler_loop((handler_t *) self) != NULL)));

    error_t *err = NULL;
    err = error_wrap("The output has been shut down", OK_IF(!self->output_shut));
    if (err) goto fail;

    err = error_from_common(vec_wrreq_push(&self->write_reqs, (tcp_write_req_t) {
        .write_req = {
            .slices = slices,
            .slice_count = slice_count,
            .written_count = 0,
        },
        .on_write = on_write,
        .on_error = on_error,
    }));
    if (err) goto fail;

    log_printf(LOG_DEBUG, "Added %p to write_reqs; have %zu of them now",
        (void *) slices,
        vec_wrreq_len(&self->write_reqs));

    poll_flags_t flags = handler_pending_mask(&self->handler);
    flags |= LOOP_WRITE;
    handler_set_pending_mask(&self->handler, flags);

fail:
    return err;
}

void tcp_shutdown_input(tcp_handler_t *self) {
    if (self->input_shut) {
        return;
    }

    error_t *err = error_from_posix(wrapper_shutdown(handler_fd(&self->handler), SHUT_RD));

    if (err) {
        error_log_free(&err, LOG_WARN, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE);
    }

    log_printf(LOG_DEBUG, "input shut down");

    self->input_shut = true;
}

void tcp_shutdown_output(tcp_handler_t *self) {
    if (self->output_shut) {
        return;
    }

    error_t *err = error_from_posix(wrapper_shutdown(handler_fd(&self->handler), SHUT_WR));

    if (err) {
        error_log_free(&err, LOG_WARN, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE);
    }

    self->output_shut = true;
}

bool tcp_is_input_shutdown(const tcp_handler_t *self) {
    return self->input_shut;
}

bool tcp_is_output_shutdown(const tcp_handler_t *self) {
    return self->output_shut;
}

void tcp_server_set_on_error(tcp_handler_server_t *self, tcp_server_on_error_cb_t on_error) {
    self->on_error = on_error;
}

void tcp_set_on_error(tcp_handler_t *self, tcp_on_error_cb_t on_error) {
    self->on_error = on_error;
}

void tcp_address(tcp_handler_t const *self, struct sockaddr const **addr, socklen_t *len) {
    *addr = &self->peer_address.addr;
    *len = self->peer_address.len;
}

void tcp_remote_info(tcp_handler_t const *self, char buf[static INET6_ADDRSTRLEN], uint16_t *port) {
    int af = self->peer_address.addr.sa_family;
    char const *result = NULL;
    errno = 0;

    switch (af) {
    case AF_INET:
        result = inet_ntop(
            af,
            &((struct sockaddr_in const *) &self->peer_address.addr)->sin_addr,
            buf,
            INET6_ADDRSTRLEN
        );
        *port = ((struct sockaddr_in const *) &self->peer_address.addr)->sin_port;

        break;

    case AF_INET6:
        result = inet_ntop(
            af,
            &((struct sockaddr_in6 const *) &self->peer_address.addr)->sin6_addr,
            buf,
            INET6_ADDRSTRLEN
        );
        *port = ((struct sockaddr_in6 const *) &self->peer_address.addr)->sin6_port;

        break;
    }

    error_assert(error_wrap("Could not convert the address to text form", error_combine(
        OK_IF(result != NULL),
        error_from_errno(errno))));
}
