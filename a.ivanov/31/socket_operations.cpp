#include "socket_operations.h"

#include "status_code.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace socket_operations {
    /*
 * When writing a server, we need to be ready to react to many kinds of event
 * which could happen next: a new connection is made, or a client sends us a
 * request, or a client drops its connection. If we make a call to, say, accept,
 * and the call blocks, then we lose our ability to respond to other events.
 * In this case you need to make a socket unblocking
 */
    int set_nonblocking(int serv_socket) {
        int option_value;
        int return_value = ioctl(serv_socket, FIONBIO, (char *) &option_value); // Set socket to be nonblocking
        if (return_value == status_code::FAIL) {
            return status_code::FAIL;
        }
        return status_code::SUCCESS;
    }

    int set_reusable(int serv_socket) {
        int option_value;
        int return_value = setsockopt(serv_socket, SOL_SOCKET, SO_REUSEADDR, // Allow socket descriptor to be reuseable
                                      (char *) &option_value, sizeof(option_value));
        if (return_value == status_code::FAIL) {
            return status_code::FAIL;
        }
        return status_code::SUCCESS;
    }

/*
 * returns non-blocking socket descriptor
 */
    int connect_to_address(char *serv_ipv4_address, int port,
                           struct timeval *timeout) {
        if (port < 0 || port >= 65536) {
            return status_code::FAIL;
        }
        int sd = socket(AF_INET, SOCK_STREAM, 0);
        if (sd == status_code::FAIL) {
            return status_code::FAIL;
        }
        struct sockaddr_in serv_sockaddr;
        serv_sockaddr.sin_family = AF_INET;
        serv_sockaddr.sin_port = htons(port);
        if (inet_pton(AF_INET, serv_ipv4_address, &serv_sockaddr.sin_addr) == status_code::FAIL) {
            return status_code::FAIL;
        }
        int opt = fcntl(sd, F_GETFL, NULL);
        if (opt < 0) {
            return status_code::FAIL;
        }
        int return_code = fcntl(sd, F_SETFL, opt | O_NONBLOCK);
        if (return_code < 0) {
            return status_code::FAIL;
        }
        return_code = connect(sd, (const struct sockaddr *) &serv_sockaddr, sizeof(serv_sockaddr));
        if (return_code == status_code::FAIL) {
            return status_code::FAIL;
        }
        return sd;
    }

    int make_new_connection_sockaddr(struct sockaddr_in *addr, int port) {
        int client_sd = socket(AF_INET, SOCK_STREAM, 0);
        if (client_sd == status_code::FAIL) {
            return status_code::FAIL;
        }
        struct sockaddr_in serv_sockaddr;
        serv_sockaddr.sin_family = AF_INET;
        serv_sockaddr.sin_port = htons(port);
        serv_sockaddr.sin_addr = addr->sin_addr;
        int return_value = connect(client_sd, (struct sockaddr *) &serv_sockaddr, sizeof(serv_sockaddr));
        if (return_value == status_code::FAIL) {
            return status_code::FAIL;
        }
        return client_sd;
    }

}
