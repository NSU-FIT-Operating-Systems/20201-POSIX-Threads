#include "url.h"

#include <errno.h>
#include <string.h>

#include "util.h"

bool url_eq(url_t const *lhs, url_t const *rhs) {
    return (
        slice_cmp(lhs->scheme, rhs->scheme) == 0 &&
        slice_cmp(lhs->username, rhs->username) == 0 &&
        slice_cmp(lhs->password, rhs->password) == 0 &&
        slice_cmp(lhs->host, rhs->host) == 0 &&
        lhs->port == rhs->port &&
        slice_cmp(lhs->path, rhs->path) == 0 &&
        slice_cmp(lhs->query, rhs->query) == 0 &&
        slice_cmp(lhs->fragment, rhs->fragment) == 0
    );
}

int url_copy(url_t const *url, url_t *result) {
    errno = 0;
    char *buf = strdup(url->buf);

    if (buf == NULL) {
        return errno;
    }

    result->buf = buf;
    result->scheme = rebase_slice(url->buf, result->buf, url->scheme);
    result->username = rebase_slice(url->buf, result->buf, url->username);
    result->password = rebase_slice(url->buf, result->buf, url->password);
    result->host = rebase_slice(url->buf, result->buf, url->host);
    result->port = url->port;
    result->path = rebase_slice(url->buf, result->buf, url->path);
    result->query = rebase_slice(url->buf, result->buf, url->query);
    result->fragment = rebase_slice(url->buf, result->buf, url->fragment);

    return 0;
}
