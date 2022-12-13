#pragma once

#include <common/posix/socket.h>

#include "common/loop/io.h"
#include "common/loop/loop.h"

// A passive (listening) TCP socket handler.
// Can be safely upcast to `handler_t`.
//
// All the methods that take `self` as a parameter must be called from a synchronized context
// (unless stated otherwise).
//
// Freeing the handler closes the underlying socket.
typedef struct tcp_handler_server tcp_handler_server_t;

// A regular TCP socket handler.
// Can be safely upcast to `handler_t`.
//
// All the methods that take `self` as a parameter must be called from a synchronized context
// (unless stated otherwise).
//
// Freeing the handler closes the underlying socket.
typedef struct tcp_handler tcp_handler_t;

// Last-resort error handler types.
typedef error_t *(*tcp_on_error_cb_t)(loop_t *loop, tcp_handler_t *handler, error_t *err);
typedef error_t *(*tcp_server_on_error_cb_t)(
    loop_t *loop,
    tcp_handler_server_t *handler,
    error_t *err
);

// Called once a connection request finishes.
typedef error_t *(*tcp_on_connect_cb_t)(loop_t *loop, tcp_handler_t *handler);
typedef error_t *(*tcp_on_connect_error_cb_t)(loop_t *loop, tcp_handler_t *handler, error_t *err);

// Called whenever a server socket is ready to accept a client connection.
typedef error_t *(*tcp_server_on_new_conn_cb_t)(loop_t *loop, tcp_handler_server_t *handler);
typedef error_t *(*tcp_server_on_listen_error_cb_t)(
    loop_t *loop,
    tcp_handler_server_t *handler,
    error_t *err
);

// Called whenever a socket has received data from the remote peer.
typedef error_t *(*tcp_on_read_cb_t)(
    loop_t *loop,
    tcp_handler_t *handler,
    slice_t buf
);

// Called whenever a handler has encountered a failure reading data from the socket.
//
// Any data read before the failure will have already been processed by the `on_read` callback.
//
// The `error` can be `NULL` if the handler was canceled.
typedef error_t *(*tcp_on_read_error_cb_t)(
    loop_t *loop,
    tcp_handler_t *handler,
    error_t *err
);

// Called whenever a handler has successfully finished writing data to the socket.
typedef error_t *(*tcp_on_write_cb_t)(loop_t *loop, tcp_handler_t *handler);

// Called whenever a handler has encountered a failure writing data to the socket.
typedef error_t *(*tcp_on_write_error_cb_t)(
    loop_t *loop,
    tcp_handler_t *handler,
    error_t *err,
    size_t written_count
);

// Allocates a new `tcp_handler_server_t`, creates a socket for it to manage, and binds it to the
// given address.
error_t *tcp_server_new(struct sockaddr *addr, socklen_t addrlen, tcp_handler_server_t **result);

// Allocates a new `tcp_handler_t`, creates a socket for it to manage, and connects it to the
// given address.
//
// Once the connection is established, calls `on_connect`.
// If an error occurs, calls `on_error` unless it's `NULL`, in which case the handler fails.
error_t *tcp_connect(
    struct sockaddr const *addr,
    socklen_t addrlen,
    tcp_on_connect_cb_t on_connect,
    tcp_on_connect_error_cb_t on_error,
    tcp_handler_t **result
);

// Accepts a pending connection from `self` and creates a new `tcp_handler_t` managing it.
error_t *tcp_accept(tcp_handler_server_t *self, tcp_handler_t **result);

// Start listening for clients.
//
// Each time the socket has a pending client, the `on_new_conn` callback will be invoked.
// If an error occurs after listening has started, the `on_error` callback will be invoked.
error_t *tcp_server_listen(
    tcp_handler_server_t *self,
    int backlog,
    tcp_server_on_new_conn_cb_t on_new_conn,
    tcp_server_on_listen_error_cb_t on_error
);

// Sets the callback to invoke whenever the socket receives data.
//
// If `on_read` is `NULL`, which is the default, the handler stops reading any data from the socket.
//
// Regardless of the value of `on_read`, the `on_error` callback can also be specified for custom
// error handling or left `NULL` to make the handler fail in such case.
//
// The connection must already be established.
void tcp_read(tcp_handler_t *self, tcp_on_read_cb_t on_read, tcp_on_read_error_cb_t on_error);

// Returns `true` if all the data has been read from the socket and no more will be received.
//
// The `eof` flag is set before the last call to the `on_read` callback.
bool tcp_is_eof(tcp_handler_t const *self);

// Creates a new request to write to the socket.
//
// The request is added to the queue.
// When the previously made requests have been processed, the handler will attempt to write the data
// from the `slices`.
// If multiple slices are provided, they are concatenated in array order.
//
// When all the data has been written, the `on_write` callback is invoked (unless `NULL`).
//
// If an error has been encountered while writing data (possibly from a previous request),
// the `on_error` callback is invoked with the count of bytes already written passed to it,
// unless `on_error` is `NULL`.
//
// The connection must already be established.
error_t *tcp_write(
    tcp_handler_t *self,
    size_t slice_count,
    slice_t const slices[static slice_count],
    tcp_on_write_cb_t on_write,
    tcp_on_write_error_cb_t on_error
);

// Shuts down the receiving end of the socket.
//
// Once all the already received data is processed, no more calls to the `on_read` callback will be
// made.
//
// This function is idempotent and can be safely called multiple times.
//
// The connection must already be established.
void tcp_shutdown_input(tcp_handler_t *self);

// Shuts down the sending end of the socket, notifying the remote peer that no more data will be
// sent to it.
//
// Any unprocessed write requests are canceled, and if they had registered `on_error` callbacks,
// they are called passing a `NULL` value as the `error` argument.
// An attempt at make a write request afterwards will return an error.
//
// This function is idempotent and can be safely called multiple times.
//
// The connection must already be established.
void tcp_shutdown_output(tcp_handler_t *self);

bool tcp_is_input_shutdown(tcp_handler_t const *self);
bool tcp_is_output_shutdown(tcp_handler_t const *self);
