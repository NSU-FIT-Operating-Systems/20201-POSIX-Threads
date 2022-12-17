#pragma once

#include "error.h"

err_t create_socket_and_connect(char const *hostname, char const *port, int *sock_fd);
err_t create_socket_and_listen(char const *port, int backlog, int *sock_fd);
void shutdown_and_close(int sock_fd);
err_t get_pending_socket_error(int sock_fd);
