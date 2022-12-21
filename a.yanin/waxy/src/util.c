#include "util.h"

#include <errno.h>

void address_to_string(
    struct sockaddr const *addr,
    socklen_t len,
    char buf[static INET6_ADDRSTRLEN]
) {
    int af = addr->sa_family;
    errno = 0;
    char const *result = inet_ntop(af, addr, buf, len);

    error_assert(error_wrap("Could not convert an address to text form",
        error_combine(OK_IF(result != NULL), error_from_errno(errno))));
}


unsigned short port_from_address(struct sockaddr const *addr) {
    switch (addr->sa_family) {
    case AF_INET:
        (void) 0;
        struct sockaddr_in const *in_addr = (struct sockaddr_in const *) addr;

        return in_addr->sin_port;

    case AF_INET6:
        (void) 0;
        struct sockaddr_in6 const *in6_addr = (struct sockaddr_in6 const *) addr;

        return in6_addr->sin6_port;

    default:
        log_abort("Unknown address family %d", addr->sa_family);
    }
}
