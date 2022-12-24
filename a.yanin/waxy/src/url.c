#include "url.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <common/error-codes/adapter.h>
#include <common/io.h>

#include "util.h"

typedef struct {
    size_t start;
    size_t len;
} region_t;

[[maybe_unused]]
static void log_url(url_t *url) {
    log_printf(LOG_DEBUG,
        "Parsed a URL:\n"
        "  .buf: %.*s [len = %zu]\n"
        "  .scheme: %.*s [len = %zu]\n"
        "  .username: %.*s [len = %zu]\n"
        "  .password: %.*s [len = %zu]\n"
        "  .host%s: %.*s [len = %zu]\n"
        "  .port%s: %u\n"
        "  .path: %.*s [len = %zu]\n"
        "  .query%s: %.*s [len = %zu]\n"
        "  .fragment%s: %.*s [len = %zu]\n",
        (int) string_len(&url->buf), string_as_cptr(&url->buf), string_len(&url->buf),
        (int) url->scheme.len, url->scheme.base, url->scheme.len,
        (int) url->username.len, url->username.base, url->username.len,
        (int) url->password.len, url->password.base, url->password.len,
        url->host_null ? "[null]" : "", (int) url->host.len, url->host.base, url->host.len,
        url->port_null ? "[null]" : "", url->port,
        (int) url->path.len, url->path.base, url->path.len,
        url->query_null ? "[null]" : "", (int) url->query.len, url->query.base, url->query.len,
        url->fragment_null ? "[null]" : "",
        (int) url->fragment.len, url->fragment.base, url->fragment.len
    );
}

static region_t region_empty(void) {
    return (region_t) {
        .start = 0,
        .len = 0,
    };
}

static slice_t region_to_slice(char const *base, region_t region) {
    assert(region.len <= ((size_t) -1) / 2);

    return (slice_t) {
        .base = base + region.start,
        .len = region.len,
    };
}

static region_t slice_to_region(char const *base, slice_t slice) {
    assert(slice.base - base >= 0);
    assert(slice.len <= ((size_t) -1) / 2);

    return (region_t) {
        .start = slice.base - base,
        .len = slice.len,
    };
}

// Like `url_t`, but doesn't use slices
typedef struct {
    region_t scheme;
    region_t username;
    region_t password;
    region_t host;
    region_t path;
    region_t query;
    region_t fragment;
    uint16_t port;
    bool host_null;
    bool port_null;
    bool query_null;
    bool fragment_null;
} url_region_t;

static bool is_c0(int c) {
    return c < 0x20;
}

static bool is_c0_space(int c) {
    return c <= 0x20;
}

static bool is_ascii_upper_alpha(int c) {
    return 'A' <= c && c <= 'Z';
}

static bool is_ascii_lower_alpha(int c) {
    return 'a' <= c && c <= 'z';
}

static bool is_ascii_alpha(int c) {
    return is_ascii_lower_alpha(c) || is_ascii_upper_alpha(c);
}

static bool is_ascii_digit(int c) {
    return '0' <= c && c <= '9';
}

static bool is_ascii_alphanum(int c) {
    return is_ascii_alpha(c) || is_ascii_digit(c);
}

static bool is_ascii_lower_hex(int c) {
    return is_ascii_digit(c) || ('a' <= c && c <= 'f');
}

static bool is_ascii_upper_hex(int c) {
    return is_ascii_digit(c) || ('A' <= c && c <= 'F');
}

static bool is_ascii_hex(int c) {
    return is_ascii_lower_hex(c) || is_ascii_upper_hex(c);
}

static bool is_windows_drive_letter(slice_t slice) {
    return slice.len == 2 && is_ascii_alpha(slice.base[0]) && (
        slice.base[1] == ':' || slice.base[1] == '|'
    );
}

static bool is_url_ascii(int c) {
    return (
        is_ascii_alphanum(c) ||
        c == '!' ||
        c == '$' ||
        c == '&' ||
        c == '\'' ||
        c == '(' ||
        c == ')' ||
        c == '*' ||
        c == '+' ||
        c == ',' ||
        c == '-' ||
        c == '.' ||
        c == '/' ||
        c == ':' ||
        c == ';' ||
        c == '=' ||
        c == '?' ||
        c == '@' ||
        c == '_' ||
        c == '~'
    );
}

static bool is_invalid_percent_escape(slice_t slice) {
    return slice.len < 2 ||
        !is_ascii_hex(slice.base[0]) ||
        !is_ascii_hex(slice.base[1]);
}

typedef enum {
    STATE_SCHEME_START,
    STATE_SCHEME,
    STATE_NO_SCHEME,
    STATE_PATH_OR_AUTH,
    STATE_SPEC_AUTH_SLASH,
    STATE_SPEC_AUTH_IGNORE_SLASH,
    STATE_AUTH,
    STATE_HOST,
    STATE_PORT,
    STATE_FILE,
    STATE_FILE_SLASH,
    STATE_FILE_HOST,
    STATE_PATH_START,
    STATE_PATH,
    STATE_OPAQUE_PATH,
    STATE_QUERY,
    STATE_FRAGMENT,
    STATE_FAIL,
} url_parser_state_t;

typedef struct {
    url_region_t *result;
    string_t const *input;
    string_t *buf;
    size_t slice_start;
    size_t pos;
    url_parser_state_t state;
    bool at_sign_seen;
    bool inside_brackets;
    bool password_token_seen;
    bool special;
} url_parser_t;

bool url_eq(url_t const *lhs, url_t const *rhs) {
    return (
        slice_cmp(lhs->scheme, rhs->scheme) == 0 &&
        slice_cmp(lhs->username, rhs->username) == 0 &&
        slice_cmp(lhs->password, rhs->password) == 0 &&
        lhs->host_null == rhs->host_null &&
        (lhs->host_null || slice_cmp(lhs->host, rhs->host) == 0) &&
        lhs->port_null == rhs->port_null &&
        (lhs->port_null || lhs->port == rhs->port) &&
        slice_cmp(lhs->path, rhs->path) == 0 &&
        lhs->query_null == rhs->query_null &&
        (lhs->query_null || slice_cmp(lhs->query, rhs->query) == 0) &&
        lhs->fragment_null == rhs->fragment_null &&
        (lhs->fragment_null || slice_cmp(lhs->fragment, rhs->fragment) == 0)
    );
}

error_t *url_copy(url_t const *url, url_t *result) {
    error_t *err = NULL;

    err = error_from_common(string_clone(&url->buf, &result->buf));
    if (err) return err;

    char const *base = string_as_cptr(&url->buf);
    char *new_base = string_as_cptr_mut(&result->buf);

    result->scheme = rebase_slice(base, new_base, url->scheme);
    result->username = rebase_slice(base, new_base, url->username);
    result->password = rebase_slice(base, new_base, url->password);
    result->host = rebase_slice(base, new_base, url->host);
    result->port = url->port;
    result->path = rebase_slice(base, new_base, url->path);
    result->query = rebase_slice(base, new_base, url->query);
    result->fragment = rebase_slice(base, new_base, url->fragment);
    result->host_null = url->host_null;
    result->port_null = url->port_null;
    result->query_null = url->query_null;
    result->fragment_null = url->fragment_null;

    return 0;
}

typedef enum {
    PERCENC_C0,
    PERCENC_FRAGMENT,
    PERCENC_QUERY,
    PERCENC_SPEC_QUERY,
    PERCENC_PATH,
    PERCENC_USERINFO,
    PERCENC_COMPONENT,
} percenc_set_t;

static bool percenc_contains(char c, percenc_set_t set) {
    switch (set) {
    case PERCENC_C0:
        return is_c0(c) || c > 0x7e;

    case PERCENC_FRAGMENT:
        return (
            c == ' ' ||
            c == '"' ||
            c == '<' ||
            c == '>' ||
            c == '`' ||
            percenc_contains(c, PERCENC_C0)
        );

    case PERCENC_QUERY:
        return (
            c == ' ' ||
            c == '"' ||
            c == '#' ||
            c == '<' ||
            c == '>' ||
            percenc_contains(c, PERCENC_C0)
        );

    case PERCENC_SPEC_QUERY:
        return c == '\'' || percenc_contains(c, PERCENC_QUERY);

    case PERCENC_PATH:
        return (
            c == '?' ||
            c == '`' ||
            c == '{' ||
            c == '}' ||
            percenc_contains(c, PERCENC_QUERY)
        );

    case PERCENC_USERINFO:
        return (
            c == '/' ||
            c == ':' ||
            c == ';' ||
            c == '=' ||
            c == '@' ||
            ('[' <= c && c <= '^') ||
            c == '|' ||
            percenc_contains(c, PERCENC_PATH)
        );

    case PERCENC_COMPONENT:
        return (
            ('$' <= c && c <= '&') ||
            c == '+' ||
            c == ',' ||
            percenc_contains(c, PERCENC_USERINFO)
        );
    }

    return false;
}

static error_t *percent_encode(char c, string_t *buf, percenc_set_t set) {
    if (percenc_contains(c, set)) {
        return error_from_common(string_appendf(buf, "%%%02x", (int) (unsigned char) c));
    } else {
        return error_from_common(string_push(buf, c));
    }
}

static error_t *url_remove_trailing(string_t *input) {
    error_t *err = NULL;

    size_t start_pos = 0;
    size_t end_pos = string_len(input);

    while (start_pos < string_len(input) && is_c0_space(string_get(input, start_pos))) {
        ++start_pos;
    }

    if (start_pos > 0) {
        err = error_combine(err,
            error_from_cstr("The input contains leading C0 control or space", NULL));
    }

    while (end_pos > start_pos && is_c0_space(string_get(input, end_pos - 1))) {
        --end_pos;
    }

    if (end_pos != string_len(input)) {
        err = error_combine(err,
            error_from_cstr("The input contains trailing C0 control or space", NULL));
    }

    size_t len = end_pos - start_pos;
    memmove(string_as_cptr_mut(input) + start_pos, string_as_cptr_mut(input), len);
    string_set_len(input, len);

    return err;
}

static error_t *url_remove_tab_nl(string_t *input) {
    size_t len = string_len(input);
    bool contains_tab_nl = false;

    for (size_t ri = 0; ri < len; ++ri) {
        size_t i = len - ri - 1;
        char c = string_get(input, i);

        if (c == '\r' || c == '\n' || c == '\t') {
            contains_tab_nl = true;
            string_remove(input, i);
        }
    }

    if (contains_tab_nl) {
        return error_from_cstr("The input contains ASCII tab or newline", NULL);
    }

    return NULL;
}

static bool is_eof(url_parser_t const *self) {
    return self->pos >= string_len(self->input);
}

static int url_parser_c(url_parser_t const *self) {
    if (is_eof(self)) {
        return EOF;
    }

    return string_get(self->input, self->pos);
}

static slice_t url_parser_buf(url_parser_t const *self) {
    size_t start = self->slice_start;
    size_t buf_len = string_len(self->buf);
    size_t len = buf_len - start;
    assert(buf_len >= start);

    return (slice_t) {
        .base = string_as_cptr(self->buf) + start,
        .len = len,
    };
}

static slice_t url_parser_slice(url_parser_t *self) {
    slice_t result = url_parser_buf(self);
    self->slice_start = string_len(self->buf);

    return result;
}

static size_t url_parser_buf_len(url_parser_t const *self) {
    return string_len(self->buf) - self->slice_start;
}

static slice_t url_parser_remaining(url_parser_t const *self) {
    if (is_eof(self)) {
        return (slice_t) {
            .base = NULL,
            .len = 0,
        };
    }

    return (slice_t) {
        .base = string_as_cptr(self->input) + self->pos + 1,
        .len = string_len(self->input) - self->pos,
    };
}

static bool url_parser_rem_starts_with(url_parser_t const *self, char const *str) {
    slice_t remaining = url_parser_remaining(self);
    size_t len = strlen(str);

    return remaining.len >= len && memcmp(remaining.base, str, len) == 0;
}

static slice_t url_parser_region_to_slice(url_parser_t const *self, region_t region) {
    return region_to_slice(string_as_cptr(self->buf), region);
}

static region_t url_parser_slice_to_region(url_parser_t const *self, slice_t slice) {
    return slice_to_region(string_as_cptr(self->buf), slice);
}

// returns the port number for the scheme if it's special
//
// return 0xffff'fffe (-2) the scheme is not special
static uint32_t url_parser_is_special(url_parser_t const *self) {
    if (slice_cmp(url_parser_region_to_slice(self, self->result->scheme),
            slice_from_cstr("ftp")) == 0) {
        return 21;
    } else if (slice_cmp(url_parser_region_to_slice(self, self->result->scheme),
            slice_from_cstr("file")) == 0) {
        return -1;
    } else if (slice_cmp(url_parser_region_to_slice(self, self->result->scheme),
            slice_from_cstr("http")) == 0) {
        return 80;
    } else if (slice_cmp(url_parser_region_to_slice(self, self->result->scheme),
            slice_from_cstr("https")) == 0) {
        return 443;
    } else if (slice_cmp(url_parser_region_to_slice(self, self->result->scheme),
            slice_from_cstr("ws")) == 0) {
        return 80;
    } else if (slice_cmp(url_parser_region_to_slice(self, self->result->scheme),
            slice_from_cstr("wss")) == 0) {
        return 443;
    } else {
        return -2;
    }
}

static error_t *url_parser_scheme_start(url_parser_t *self) {
    error_t *err = NULL;

    int c = url_parser_c(self);

    if (is_ascii_alpha(c)) {
        err = error_from_common(string_push(self->buf, tolower(c)));

        if (err) {
            self->state = STATE_FAIL;
            goto fail;
        }

        self->state = STATE_SCHEME;
    } else {
        self->state = STATE_NO_SCHEME;
        --self->pos;
    }

fail:
    return err;
}

static error_t *url_parser_scheme(url_parser_t *self) {
    error_t *err = NULL;

    int c = url_parser_c(self);

    if (is_ascii_alphanum(c) || c == '+' || c == '-' || c == '.') {
        err = error_from_common(string_push(self->buf, tolower(c)));

        if (err) {
            self->state = STATE_FAIL;
            goto fail;
        }
    } else if (c == ':') {
        slice_t scheme = url_parser_slice(self);
        self->result->scheme = url_parser_slice_to_region(self, scheme);
        self->special = url_parser_is_special(self);

        if (slice_cmp(url_parser_region_to_slice(self, self->result->scheme),
                slice_from_cstr("file")) == 0) {
            err = error_combine(err, error_wrap("`//` is required after the file scheme",
                OK_IF(url_parser_rem_starts_with(self, "//"))));
            self->state = STATE_FILE;
        } else if (self->special) {
            self->state = STATE_SPEC_AUTH_SLASH;
        } else if (url_parser_rem_starts_with(self, "/")) {
            self->state = STATE_PATH_OR_AUTH;
            ++self->pos;
        } else {
            self->state = STATE_OPAQUE_PATH;
        }
    } else {
        string_clear(self->buf);
        self->state = STATE_NO_SCHEME;
        self->pos = 0;
    }

fail:
    return err;
}

static error_t *url_parser_no_scheme(url_parser_t *self) {
    error_t *err = NULL;

    err = error_from_cstr("No scheme was provided", NULL);
    self->state = STATE_FAIL;

    return err;
}

static error_t *url_parser_path_or_auth(url_parser_t *self) {
    error_t *err = NULL;

    if (url_parser_c(self) == '/') {
        self->state = STATE_AUTH;
    } else {
        self->state = STATE_PATH;
        --self->pos;
    }

    return err;
}

static error_t *url_parser_spec_auth_slash(url_parser_t *self) {
    error_t *err = NULL;

    if (url_parser_c(self) == '/' && url_parser_rem_starts_with(self, "/")) {
        self->state = STATE_SPEC_AUTH_IGNORE_SLASH;
        ++self->pos;
    } else {
        err = error_from_cstr("`//` is required for a special scheme", NULL);
        self->state = STATE_SPEC_AUTH_IGNORE_SLASH;
        --self->pos;
    }

    return err;
}

static error_t *url_parser_spec_auth_ignore_slash(url_parser_t *self) {
    error_t *err = NULL;

    int c = url_parser_c(self);

    if (c != '/' && c != '\\') {
        self->state = STATE_AUTH;
        --self->pos;
    } else {
        err = error_from_cstr("encountered an extraneous `/`", NULL);
    }

    return err;
}

static error_t *url_parser_auth(url_parser_t *self) {
    error_t *err = NULL;

    int c = url_parser_c(self);

    if (c == '@') {
        err = error_from_cstr(
            "encountered an illegal `@` (a valid URL cannot contain a username)",
            NULL
        );

        if (self->at_sign_seen) {
            error_t *append_err = error_from_common(string_appendf(self->buf, "%%40"));

            if (append_err) {
                err = error_combine(append_err, err);
                self->state = STATE_FAIL;
                goto fail;
            }
        }

        self->at_sign_seen = true;
        region_t buf = url_parser_slice_to_region(self, url_parser_slice(self));
        self->result->username.start = buf.len;

        for (size_t i = 0; i < buf.len; ++i) {
            slice_t buf_slice = url_parser_region_to_slice(self, buf);

            if (buf_slice.base[i] == ':' && !self->password_token_seen) {
                self->password_token_seen = true;
                self->result->password.start = string_len(self->buf);

                continue;
            }

            error_t *encode_err = percent_encode(buf_slice.base[i], self->buf, PERCENC_USERINFO);
            // XXX: `buf_slice` has been invalidated and must not be used

            if (encode_err) {
                err = error_combine(encode_err, err);
                self->state = STATE_FAIL;
                goto fail;
            }

            if (self->password_token_seen) {
                ++self->result->password.len;
            } else {
                ++self->result->username.len;
            }
        }
    } else if (c == EOF || c == '/' || c == '?' || c == '#' || (self->special && c == '\\')) {
        if (self->at_sign_seen && url_parser_buf_len(self) == 0) {
            err = error_combine(error_from_cstr("Expected a hostname after the user info", NULL),
                err);
            self->state = STATE_FAIL;
            goto fail;
        }

        slice_t buf = url_parser_slice(self);

        for (size_t i = 0; i < buf.len; ++i) {
            string_pop(self->buf);
        }

        self->slice_start = string_len(self->buf);
        self->pos -= buf.len + 1;
        self->state = STATE_HOST;
    } else {
        error_t *push_err = error_from_common(string_push(self->buf, c));

        if (push_err) {
            err = error_combine(push_err, err);
            self->state = STATE_FAIL;
            goto fail;
        }
    }

fail:
    return err;
}

static error_t *url_parser_host(url_parser_t *self) {
    error_t *err = NULL;

    int c = url_parser_c(self);

    if (c == ':' && !self->inside_brackets) {
        if (url_parser_buf_len(self) == 0) {
            err = error_from_cstr("The hostname is empty", NULL);
            self->state = STATE_FAIL;
            goto fail;
        }

        self->result->host = url_parser_slice_to_region(self, url_parser_slice(self));
        self->result->host_null = false;
        self->state = STATE_PORT;
    } else if (c == EOF || c == '/' || c == '?' || c == '#' || (self->special && c == '\\')) {
        --self->pos;

        if (self->special && url_parser_buf_len(self) == 0) {
            err = error_from_cstr("The hostname is empty", NULL);
            self->state = STATE_FAIL;
            goto fail;
        }

        self->result->host = url_parser_slice_to_region(self, url_parser_slice(self));
        self->result->host_null = false;
        self->state = STATE_PATH;
    } else {
        if (c == '[') {
            self->inside_brackets = true;
        }

        if (c == ']') {
            self->inside_brackets = false;
        }

        err = error_from_common(string_push(self->buf, c));

        if (err) {
            self->state = STATE_FAIL;
            goto fail;
        }
    }

fail:
    return err;
}

static error_t *url_parser_port(url_parser_t *self) {
    error_t *err = NULL;

    int c = url_parser_c(self);

    if (is_ascii_digit(c)) {
        err = error_from_common(string_push(self->buf, c));

        if (err) {
            self->state = STATE_FAIL;
            goto fail;
        }
    } else if (c == EOF || c == '/' || c == '?' || c == '#' || (self->special && c == '\\')) {
        if (url_parser_buf_len(self) != 0) {
            slice_t buf = url_parser_slice(self);
            int64_t port = -1;
            err = error_wrap("Could not parse the port number", error_from_common(
                parse_integer((unsigned char const *) buf.base, buf.len, &port)));

            if (port > 65535) {
                err = error_from_cstr("The port nubmer is too high", NULL);
                self->state = STATE_FAIL;
                goto fail;
            }

            self->result->port = (uint16_t) port;
            self->result->port_null = false;
        }

        --self->pos;
        self->state = STATE_PATH_START;
    } else {
        err = error_from_cstr("The port contains illegal characters", NULL);
        self->state = STATE_FAIL;
        goto fail;
    }

fail:
    return err;
}

static error_t *url_parser_file(url_parser_t *self) {
    error_t *err = NULL;

    if (slice_cmp(url_parser_region_to_slice(self, self->result->scheme),
            slice_from_cstr("file")) != 0) {
        err = error_from_common(string_appendf(self->buf, "file"));

        if (err) {
            self->state = STATE_FAIL;
            goto fail;
        }

        self->result->scheme = url_parser_slice_to_region(self, url_parser_slice(self));
    }

    self->result->host = region_empty();
    self->result->host_null = false;
    int c = url_parser_c(self);

    if (c == '/' || c == '\\') {
        if (c == '\\') {
            err = error_from_cstr("A backslash may not be used in a file: URL", NULL);
            self->state = STATE_FAIL;
            goto fail;
        }

        self->state = STATE_FILE_SLASH;
    } else {
        --self->pos;
        self->state = STATE_PATH;
    }

fail:
    return err;
}

static error_t *url_parser_file_slash(url_parser_t *self) {
    error_t *err = NULL;

    int c = url_parser_c(self);

    if (c == '/' || c == '\\') {
        if (c == '\\') {
            err = error_from_cstr("A backslash may not be used in a file: URL", NULL);
            self->state = STATE_FAIL;
            goto fail;
        }

        self->state = STATE_FILE_HOST;
    } else {
        self->state = STATE_PATH;
        --self->pos;
    }

fail:
    return err;
}

static error_t *url_parser_file_host(url_parser_t *self) {
    error_t *err = NULL;

    int c = url_parser_c(self);

    if (c == EOF || c == '/' || c == '\\' || c == '?' || c == '#') {
        --self->pos;

        if (is_windows_drive_letter(url_parser_buf(self))) {
            // this is a windows drive letter
            // this is actually a thing
            // in the URL standard
            err = error_from_cstr("A Windows drive letter cannot be used in a URL", NULL);
            self->state = STATE_PATH;
        } else if (url_parser_buf_len(self) == 0) {
            self->result->host = region_empty();
            self->result->host_null = false;
            self->state = STATE_PATH_START;
        } else {
            self->result->host = url_parser_slice_to_region(self, url_parser_slice(self));
            self->result->host_null = false;
            self->state = STATE_PATH_START;
        }
    } else {
        err = error_from_common(string_push(self->buf, c));

        if (err) {
            self->state = STATE_FAIL;
            goto fail;
        }
    }

fail:
    return err;
}

static error_t *url_parser_path_start(url_parser_t *self) {
    error_t *err = NULL;

    int c = url_parser_c(self);

    if (self->special) {
        if (c == '\\') {
            err = error_from_cstr("Backslashes are not allowed in a URL", NULL);
        }

        self->state = STATE_PATH;

        if (c != '/' && c != '\\') {
            --self->pos;
        }
    } else if (c == '?') {
        self->result->query = region_empty();
        self->result->query_null = false;
        self->state = STATE_QUERY;
    } else if (c == '#') {
        self->result->fragment = region_empty();
        self->result->fragment_null = false;
        self->state = STATE_FRAGMENT;
    } else if (c != EOF) {
        self->state = STATE_PATH;

        if (c != '/') {
            --self->pos;
        }
    }

    return err;
}

static error_t *url_parser_path(url_parser_t *self) {
    error_t *err = NULL;

    int c = url_parser_c(self);

    if (c == EOF || c == '/' || (self->special && c == '\\') || c == '?' || c == '#') {
        if (self->special && c == '\\') {
            err = error_from_cstr("Backslashes are not allowed in a URL", NULL);
        }

        if (slice_cmp(url_parser_region_to_slice(self, self->result->scheme),
                    slice_from_cstr("file")) == 0 &&
                self->result->path.len == 0 &&
                is_windows_drive_letter(url_parser_buf(self))) {
            *string_get_mut(self->buf, string_len(self->buf) - 1) = ':';
        }

        if (c == '/' || (self->special && c == '\\')) {
            error_t *push_err = error_from_common(string_push(self->buf, c));

            if (push_err) {
                err = error_combine(push_err, err);
                self->state = STATE_FAIL;
                goto fail;
            }
        }

        slice_t buf = url_parser_slice(self);

        if (self->result->path.len == 0) {
            self->result->path = url_parser_slice_to_region(self, buf);
        } else {
            self->result->path.len += buf.len;
        }

        if (c == '?') {
            self->result->query = region_empty();
            self->result->query_null = false;
            self->state = STATE_QUERY;
        }

        if (c == '#') {
            self->result->fragment = region_empty();
            self->result->fragment_null = false;
            self->state = STATE_FRAGMENT;
        }
    } else {
        if (!is_url_ascii(c) && c < 0x80) {
            err = error_from_cstr("An illegal character was detected in the path", NULL);
        }

        if (c == '%' && is_invalid_percent_escape(url_parser_remaining(self))) {
            err = error_from_cstr("A percent escape is not followed by two hex digits", NULL);
        }

        error_t *encode_err = percent_encode(c, self->buf, PERCENC_PATH);

        if (encode_err) {
            err = error_combine(encode_err, err);
            self->state = STATE_FAIL;
            goto fail;
        }
    }

fail:
    return err;
}

static error_t *url_parser_opaque_path(url_parser_t *self) {
    error_t *err = NULL;

    int c = url_parser_c(self);

    if (c == '?') {
        self->result->path = url_parser_slice_to_region(self, url_parser_slice(self));
        self->result->query = region_empty();
        self->result->query_null = false;
        self->state = STATE_QUERY;
    } else if (c == '#') {
        self->result->path = url_parser_slice_to_region(self, url_parser_slice(self));
        self->result->fragment = region_empty();
        self->result->fragment_null = false;
        self->state = STATE_FRAGMENT;
    } else {
        if (c != EOF && !is_url_ascii(c) && c < 0x80 && c != '%') {
            err = error_from_cstr("An illegal character was detected in the path", NULL);
        }

        if (c == '%' && is_invalid_percent_escape(url_parser_remaining(self))) {
            err = error_from_cstr("A percent escape is not followed by two hex digits", NULL);
        }

        if (c != EOF) {
            error_t *encode_err = percent_encode(c, self->buf, PERCENC_C0);

            if (encode_err) {
                err = error_combine(encode_err, err);
                self->state = STATE_FAIL;
                goto fail;
            }
        }
    }

fail:
    return err;
}

static error_t *url_parser_query(url_parser_t *self) {
    error_t *err = NULL;

    int c = url_parser_c(self);

    if (c == '#' || c == EOF) {
        self->result->query = url_parser_slice_to_region(self, url_parser_slice(self));
        self->result->query_null = false;

        if (c == '#') {
            self->result->fragment = region_empty();
            self->result->fragment_null = false;
            self->state = STATE_FRAGMENT;
        }
    } else if (c != EOF) {
        if (!is_url_ascii(c) && c != '%') {
            err = error_from_cstr("An illegal character was detected in the query", NULL);
        }

        if (c == '%' && is_invalid_percent_escape(url_parser_remaining(self))) {
            err = error_from_cstr("A percent escape is not followed by two hex digits", NULL);
        }

        percenc_set_t set = self->special ? PERCENC_SPEC_QUERY : PERCENC_QUERY;
        error_t *encode_err = percent_encode(c, self->buf, set);

        if (encode_err) {
            err = error_combine(encode_err, err);
            self->state = STATE_FAIL;
            goto fail;
        }
    }

fail:
    return err;
}

static error_t *url_parser_fragment(url_parser_t *self) {
    error_t *err = NULL;

    int c = url_parser_c(self);

    if (c == EOF) {
        self->result->fragment = url_parser_slice_to_region(self, url_parser_slice(self));
        self->result->fragment_null = false;
    } else {
        if (!is_url_ascii(c) && c != '%') {
            err = error_from_cstr("An illegal character was detected in the fragment", NULL);
        }

        if (c == '%' && is_invalid_percent_escape(url_parser_remaining(self))) {
            err = error_from_cstr("A percent escape is not followed by two hex digits", NULL);
        }

        error_t *encode_err = percent_encode(c, self->buf, PERCENC_FRAGMENT);

        if (encode_err) {
            err = error_combine(encode_err, err);
            self->state = STATE_FAIL;
            goto fail;
        }
    }

fail:
    return err;
}

// A partial implementation of https://url.spec.whatwg.org/#concept-basic-url-parser
//
// Doesn't support relative URLs because I don't care about them.
error_t *url_parse(slice_t slice, url_t *result, bool *fatal) {
    error_t *err = NULL;
    *fatal = true;

    string_t input;
    err = error_from_common(string_from_slice(slice.base, slice.len, &input));
    if (err) goto input_copy_fail;

    string_t buf;
    err = error_from_common(string_new(&buf));
    if (err) goto buf_new_fail;

    err = error_combine(err, url_remove_trailing(&input));
    err = error_combine(err, url_remove_tab_nl(&input));

    // the slices should point to the buffer **after** the parsing is done
    // since the buffer keeps being reallocated, we have to store indices during parsing
    url_region_t region_url = {
        .scheme = region_empty(),
        .username = region_empty(),
        .password = region_empty(),
        .host = region_empty(),
        .port = 0,
        .path = region_empty(),
        .query = region_empty(),
        .fragment = region_empty(),
        .host_null = true,
        .port_null = true,
        .query_null = true,
        .fragment_null = true,
    };

    url_parser_t parser = {
        .result = &region_url,
        .input = &input,
        .buf = &buf,
        .slice_start = 0,
        .pos = -1,
        .state = STATE_SCHEME_START,
        .at_sign_seen = false,
        .inside_brackets = false,
        .password_token_seen = false,
        .special = false,
    };

    do {
        ++parser.pos;

        switch (parser.state) {
        case STATE_SCHEME_START:
            err = error_combine(err, url_parser_scheme_start(&parser));
            break;

        case STATE_SCHEME:
            err = error_combine(err, url_parser_scheme(&parser));
            break;

        case STATE_NO_SCHEME:
            err = error_combine(err, url_parser_no_scheme(&parser));
            break;

        case STATE_PATH_OR_AUTH:
            err = error_combine(err, url_parser_path_or_auth(&parser));
            break;

        case STATE_SPEC_AUTH_IGNORE_SLASH:
            err = error_combine(err, url_parser_spec_auth_ignore_slash(&parser));
            break;

        case STATE_SPEC_AUTH_SLASH:
            err = error_combine(err, url_parser_spec_auth_slash(&parser));
            break;

        case STATE_AUTH:
            err = error_combine(err, url_parser_auth(&parser));
            break;

        case STATE_HOST:
            err = error_combine(err, url_parser_host(&parser));
            break;

        case STATE_PORT:
            err = error_combine(err, url_parser_port(&parser));
            break;

        case STATE_FILE:
            err = error_combine(err, url_parser_file(&parser));
            break;

        case STATE_FILE_SLASH:
            err = error_combine(err, url_parser_file_slash(&parser));
            break;

        case STATE_FILE_HOST:
            err = error_combine(err, url_parser_file_host(&parser));
            break;

        case STATE_PATH_START:
            err = error_combine(err, url_parser_path_start(&parser));
            break;

        case STATE_PATH:
            err = error_combine(err, url_parser_path(&parser));
            break;

        case STATE_OPAQUE_PATH:
            err = error_combine(err, url_parser_opaque_path(&parser));
            break;

        case STATE_QUERY:
            err = error_combine(err, url_parser_query(&parser));
            break;

        case STATE_FRAGMENT:
            err = error_combine(err, url_parser_fragment(&parser));
            break;

        case STATE_FAIL:
            goto parser_fail;
        }
    } while (!is_eof(&parser));

    *result = (url_t) {
        .buf = buf,
        .scheme = url_parser_region_to_slice(&parser, region_url.scheme),
        .username = url_parser_region_to_slice(&parser, region_url.username),
        .password = url_parser_region_to_slice(&parser, region_url.password),
        .host = url_parser_region_to_slice(&parser, region_url.host),
        .port = region_url.port,
        .path = url_parser_region_to_slice(&parser, region_url.path),
        .query = url_parser_region_to_slice(&parser, region_url.query),
        .fragment = url_parser_region_to_slice(&parser, region_url.fragment),
        .host_null = region_url.host_null,
        .port_null = region_url.port_null,
        .query_null = region_url.query_null,
        .fragment_null = region_url.fragment_null,
    };

    *fatal = false;
    log_url(result);

parser_fail:
    if (*fatal) {
        string_free(&buf);
    }

buf_new_fail:
    string_free(&input);

input_copy_fail:
    return err;
}
