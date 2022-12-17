#ifndef HTTP_CPP_PROXY_SOCKET_OPERATIONS_H
#define HTTP_CPP_PROXY_SOCKET_OPERATIONS_H

#include <arpa/inet.h>

namespace socket_operations {

    int set_nonblocking(int serv_socket);

    int set_reusable(int serv_socket);

    int connect_to_address(char *serv_ipv4_address, int port);

    int make_new_connection_sockaddr(struct sockaddr_in *addr, int port);
}

#endif //HTTP_CPP_PROXY_SOCKET_OPERATIONS_H
