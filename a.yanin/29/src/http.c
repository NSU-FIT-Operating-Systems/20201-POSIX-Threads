#include "http.h"

#include <ctype.h>
#include <string.h>

#include <common/log/log.h>
#include <common/io.h>
#include <common/posix/io.h>
#include <common/collections/string.h>

#include "io.h"
#include "socket.h"

#define VEC_ELEMENT_TYPE string_t
#define VEC_LABEL string
#define VEC_CONFIG (COLLECTION_DEFINE | COLLECTION_DECLARE | COLLECTION_STATIC)
#include <common/collections/vec.h>

typedef struct {
    string_t scheme;
    string_t host;
    string_t port;
    string_t path;
    string_t query;
} url_t;

static bool is_valid_url_string(string_t const *str) {
    for (size_t i = 0; i < string_len(str); ++i) {
        unsigned char c = string_get(str, i);

        if (c <= 0x1f || c > 0x7f || // c0 controls
                strchr(" \"<>`#?{}", c) != NULL) {
            return false;
        }
    }

    return true;
}

// a parser that's subtly incorrect is worse than one that's plainly wrong
// below is an example of the former, because URLs are rather complex and I can't be bothered enough
// to sort through that
static err_t parse_url(char const *url, url_t *result) {
    err_t error = OK;

    char const *p = url;
    char const *end = url + strlen(url);
    char const *scheme_end = strchr(p, ':');

    if (ERR_FAILED(error = ERR((bool)(scheme_end != NULL && scheme_end != p),
            "invalid URL: scheme is absent"))) goto scheme_fail;

    if (ERR_FAILED(error = ERR(string_from_slice(p, scheme_end - p, &result->scheme),
            "failed to allocate a string"))) goto scheme_copy_fail;

    p = scheme_end + 1;

    if (strncmp(p, "//", 2) == 0) {
        // authority
        p += 2;

        char const *authority_end = strchr(p, '/');

        if (authority_end == NULL) {
            // no path
            authority_end = end;
        }

        error = ERR((bool)(authority_end != p), "unsupported URL: an empty authority");
        if (ERR_FAILED(error)) goto authority_fail;

        size_t authority_len = authority_end - p;
        char const *at_chr = memchr(p, '@', authority_len);

        error = ERR((bool)(at_chr == NULL), "URLs with a userinfo component are not supported");
        if (ERR_FAILED(error)) goto authority_fail;

        char const *colon_chr = memchr(p, ':', authority_len);
        char const *host_end = colon_chr;

        if (host_end == NULL) {
            host_end = authority_end;
        }

        if (ERR_FAILED(error = ERR(string_from_slice(p, host_end - p, &result->host),
                "failed to allocate a string"))) goto host_copy_fail;

        if (colon_chr != NULL) {
            // port
            p = colon_chr + 1;
            char const *port_end = authority_end;
            char const *port_start = p;

            for (; p != port_end; ++p) {
                if (ERR_FAILED(error = ERR((bool) isdigit(*p),
                        "invalid URL: the port is not valid"))) goto port_fail;
            }

            error = ERR(string_from_slice(port_start, port_end - port_start, &result->port),
                "failed to allocate a string");
            if (ERR_FAILED(error)) goto port_copy_fail;
        } else {
            if (ERR_FAILED(error = ERR(string_new(&result->port),
                    "failed to allocate a string"))) goto port_copy_fail;
        }

        p = authority_end;
    } else {
        if (ERR_FAILED(error = ERR(string_new(&result->host),
                "failed to allocate a string"))) goto host_copy_fail;

        if (ERR_FAILED(error = ERR(string_new(&result->port),
                "failed to allocate a string"))) goto port_copy_fail;
    }

    if (*p == '\0') {
        // empty path component
        if (ERR_FAILED(error = ERR(string_from_cstr("/", &result->path),
                "failed to allocate a string"))) goto path_copy_fail;

        if (ERR_FAILED(error = ERR(string_new(&result->query),
                "failed to allocate a string"))) goto query_copy_fail;
    } else {
        // non-empty path (+ optional query)
        if (ERR_FAILED(error = ERR((bool)(*p == '/'),
                "invalid URL: expected a path component"))) goto path_fail;

        char const *path_end = strchr(p, '?');

        if (path_end == NULL) {
            path_end = end;
        }

        if (ERR_FAILED(error = ERR(string_from_slice(p, path_end - p, &result->path),
                "failed to allocate a string"))) goto path_copy_fail;

        p = path_end;

        if (*p == '?') {
            // query
            if (ERR_FAILED(error = ERR(string_from_slice(p, end - p, &result->query),
                    "failed to allocate a string"))) goto query_copy_fail;
        } else {
            if (ERR_FAILED(error = ERR(string_new(&result->query),
                    "failed to allocate a string"))) goto query_copy_fail;
        }
    }

    if (ERR_FAILED(error = ERR(is_valid_url_string(&result->scheme),
            "invalid URL: unexpected characters in the scheme"))) goto validate_fail;
    if (ERR_FAILED(error = ERR(is_valid_url_string(&result->host),
            "invalid URL: unexpected characters in the host"))) goto validate_fail;
    // port is already checked
    if (ERR_FAILED(error = ERR(is_valid_url_string(&result->path),
            "invalid URL: unexpected characters in the host"))) goto validate_fail;
    if (ERR_FAILED(error = ERR(is_valid_url_string(&result->query),
            "invalid URL: unexpected characters in the host"))) goto validate_fail;

    return error;

validate_fail:
    string_free(&result->query);

query_copy_fail:
    string_free(&result->path);

path_copy_fail:
path_fail:
    string_free(&result->port);

port_copy_fail:
port_fail:
    string_free(&result->host);

host_copy_fail:
authority_fail:
    string_free(&result->scheme);

scheme_copy_fail:
scheme_fail:
    return error;
}

static void url_free(url_t *url) {
    string_free(&url->scheme);
    string_free(&url->host);
    string_free(&url->port);
    string_free(&url->path);
    string_free(&url->query);
}

static char const request_fmt[] =
    "GET %s%s HTTP/1.1\r\n"
    "User-Agent: http-printer\r\n"
    "Host: %s\r\n"
    "Connection: close\r\n"
    "\r\n";

err_t http_get(char const *url, int *sock_fd) {
    assert(url != NULL);
    assert(sock_fd != NULL);

    err_t error = OK;

    url_t url_components;

    if (ERR_FAILED(error = ERR(parse_url(url, &url_components),
            "failed to parse the URL"))) goto parse_url_fail;

    if (ERR_FAILED(error = ERR((bool)(strcmp(string_as_cptr(&url_components.scheme), "http") == 0),
            "unsupported scheme"))) goto unsupported_scheme;

    string_t request;
    error = ERR(string_sprintf(&request, request_fmt,
        string_as_cptr(&url_components.path), string_as_cptr(&url_components.query),
        string_as_cptr(&url_components.host)), "failed to allocate a string");
    if (ERR_FAILED(error)) goto request_sprintf_fail;

    char const *host = string_as_cptr(&url_components.host);
    char const *port = string_as_cptr(&url_components.port);

    if (string_len(&url_components.port) == 0) {
        port = "80";
    }

    int fd = -1;

    if (ERR_FAILED(error = ERR(create_socket_and_connect(host, port, &fd),
            "failed to connect to the server"))) goto connect_fail;

    if (ERR_FAILED(error = ERR(write_all(fd, string_as_ptr(&request), string_len(&request)),
            "failed to send a request to the server"))) goto send_fail;

    *sock_fd = fd;

send_fail:
connect_fail:
    string_free(&request);

request_sprintf_fail:
unsupported_scheme:
    url_free(&url_components);

parse_url_fail:
    return error;
}
