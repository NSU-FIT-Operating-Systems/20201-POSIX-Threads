#include <assert.h>
#include <string.h>

#define LOG_LEVEL LOG_INFO
#include <common/log/log.h>

#include <common/posix/file.h>
#include <common/posix/io.h>
#include <common/posix/ipc.h>
#include <common/posix/signal.h>
#include <common/posix/tty.h>

#include "error.h"
#include "io.h"
#include "http.h"
#include "socket.h"

enum {
    INITIAL_BUF_SIZE = 4 * 1024 * 1024,
    MIN_READ_SIZE = 512 * 1024 * 1024,
    LINE_LIMIT = 25,
};

static void pollfd_toggle_ignore(int *fd) {
    *fd = -*fd - 1;
}

static void print_usage(void) {
    fputs("Usage: http-printer <url>\n", stderr);
}

static int signal_fd_write = -1;

static void write_to_signal_pipe(int signal) {
    wrapper_write(signal_fd_write, &(char) {signal}, 1, NULL);
}

static err_t setup_signal_fd(int *signals, size_t n, int *signal_fd) {
    assert(signals != NULL);
    assert(signal_fd != NULL);

    err_t error = OK;

    error = ERR(wrapper_pipe(signal_fd, &signal_fd_write), "failed to create a signal pipe");

    if (ERR_FAILED(error)) goto pipe_fail;

    for (size_t i = 0; i < n; ++i) {
        sigaction(signals[i], &(struct sigaction) { .sa_handler = write_to_signal_pipe }, NULL);
    }

pipe_fail:
    return error;
}

static void tear_down_signal_fd(int signal_fd) {
    assert(signal_fd >= 0);

    err_t warn = OK;

    warn = ERR(wrapper_close(signal_fd_write), "failed to close the write end of a signal pipe");

    if (ERR_FAILED(warn)) {
        err_log_free(LOG_WARN, &warn);
    }

    warn = ERR(wrapper_close(signal_fd), "failed to close the read end of a signal pipe");

    if (ERR_FAILED(warn)) {
        err_log_free(LOG_WARN, &warn);
    }
}

static err_t read_signal(int signal_fd, bool *running) {
    assert(signal_fd >= 0);

    err_t error = OK;

    int8_t signal = -1;
    ssize_t read_count = -1;

    if (ERR_FAILED(error = ERR(wrapper_read(signal_fd, &signal, 1, &read_count),
            "failed to read from the signal pipe"))) {
        return error;
    }

    if (ERR_FAILED(error = ERR((bool)(read_count != 0),
            "the signal pipe was closed unexpectedly"))) {
        return error;
    }

    if (signal == SIGINT) {
        *running = false;
    }

    return error;
}

static void stop_waiting_for_user(bool *waiting_for_user) {
    *waiting_for_user = false;
    fputs("\r\x1b[K", stderr);
    fflush(stderr);
}

static err_t read_input(
    int in_fd,
    struct pollfd *in_pollfd,
    bool *should_paginate,
    bool *waiting_for_user
) {
    assert(in_fd >= 0);
    assert(in_pollfd != NULL);
    assert(should_paginate != NULL);
    assert(waiting_for_user != NULL);

    err_t error = OK;

    char c = -1;
    ssize_t read_count = -1;

    if (ERR_FAILED(error = ERR(wrapper_read(in_fd, &c, 1, &read_count),
            "failed to read from stdin"))) {
        return error;
    }

    if (read_count == 0) {
        *should_paginate = false;
        stop_waiting_for_user(waiting_for_user);
        pollfd_toggle_ignore(&in_pollfd->fd);

        return error;
    }

    if (!*waiting_for_user) {
        return error;
    }

    if (c == ' ') {
        stop_waiting_for_user(waiting_for_user);
    }

    return error;
}

static err_t read_response(int sock_fd, struct pollfd *sock_pollfd, buf_t *response, bool *eof) {
    assert(sock_fd >= 0);
    assert(sock_pollfd != NULL);
    assert(response != NULL);

    err_t error = OK;

    if (sock_pollfd->revents & POLLERR) {
        error = get_pending_socket_error(sock_fd);

        return error;
    }

    if (sock_pollfd->revents & POLLHUP) {
        return error;
    }

    if (sock_pollfd->revents & POLLIN) {
        if (buf_immediately_available_write_size(response) < MIN_READ_SIZE) {
            error = ERR(buf_ensure_enough_write_space(response, INITIAL_BUF_SIZE), NULL);

            if (ERR_FAILED(error)) return error;
        }

        ssize_t read_count = 0;
        size_t buf_size = buf_immediately_available_write_size(response);
        char *buf = buf_get_write_ptr(response);
        log_printf(LOG_DEBUG, "Calling wrapper_read(%d, %p, %zu, %p)", sock_fd, (void *) buf, buf_size, (void *) &read_count);
        posix_err_t status = wrapper_read(sock_fd, buf, buf_size, &read_count);
        log_printf(LOG_DEBUG, "wrapper_read returned code %d, size %zu", status.errno_code, read_count);

        if (ERR_FAILED(error = ERR(buf_write(response, NULL, read_count), NULL))) {
            return error;
        }

        if (ERR_FAILED(error = ERR(status, "failed to read the server response"))) {
            return error;
        } else if (read_count == 0) {
            *eof = true;
            pollfd_toggle_ignore(&sock_pollfd->fd);
            log_printf(LOG_DEBUG, "Setting eof");
        }
    }

    return error;
}

static err_t print_lines(buf_t *response, size_t *line_count, bool should_paginate) {
    assert(response != NULL);
    assert(line_count != NULL);

    err_t error = OK;

    size_t byte_count = buf_available_read_size(response);
    char const *start = buf_get_read_ptr(response);
    char const *last_line = start;
    char const *end = start + byte_count;

    for (; last_line < end && (!should_paginate || *line_count < LINE_LIMIT); ++(*line_count)) {
        last_line = memchr(last_line, '\n', end - last_line - 1);

        if (last_line == NULL) {
            last_line = end;

            break;
        }

        ++last_line;
    }

    size_t count = last_line - start;
    error = ERR(write_all(STDOUT_FILENO, (unsigned char const *) start, count),
        "failed to write the received lines");
    buf_read(response, NULL, count);

    return error;
}

static err_t run_printer(int sock_fd, int signal_fd, bool should_paginate) {
    assert(sock_fd >= 0);
    assert(signal_fd >= 0);

    err_t error = OK;

    size_t line_count = 0;
    bool waiting_for_user = false;

    struct pollfd fds[] = {
        { .fd = sock_fd, .events = POLLIN },
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = signal_fd, .events = POLLIN },
    };
    size_t fd_count = sizeof(fds) / sizeof(*fds);

    struct pollfd *sock_pollfd = &fds[0];
    struct pollfd *stdin_pollfd = &fds[1];
    struct pollfd *signal_pollfd = &fds[2];

    if (!should_paginate) {
        stdin_pollfd->fd = stdin_pollfd->fd - 1;
    }

    bool running = true;
    bool eof = false;
    buf_t response;

    if (ERR_FAILED(error = ERR(buf_new(INITIAL_BUF_SIZE, &response),
            "failed to allocate the buffer"))) goto buf_new_fail;

    while (running) {
        int poll_count = -1;

        if (ERR_FAILED(error = ERR(wrapper_poll(fds, fd_count, -1, &poll_count), NULL))) {
            goto poll_fail;
        }

        if (signal_pollfd->revents != 0) {
            if (ERR_FAILED(error = ERR(read_signal(signal_fd, &running), NULL))) {
                goto handle_fail;
            }

            continue;
        }

        if (stdin_pollfd->revents != 0) {
            error = ERR(read_input(STDIN_FILENO, stdin_pollfd, &should_paginate,
                &waiting_for_user), NULL);

            if (ERR_FAILED(error)) goto handle_fail;
        }

        if (sock_pollfd->revents != 0) {
            error = ERR(read_response(sock_fd, sock_pollfd, &response, &eof), NULL);

            if (ERR_FAILED(error)) goto handle_fail;
        }

        if (!waiting_for_user) {
            print_lines(&response, &line_count, should_paginate);

            if (eof && buf_available_read_size(&response) == 0) {
                break;
            }
        }

        if (should_paginate && line_count == LINE_LIMIT) {
            waiting_for_user = true;
            line_count = 0;
            fputs("\x1b[36mPress space to continue...\x1b[m", stderr);
            fflush(stderr);
        }
    }

poll_fail:
handle_fail:
    buf_free(&response);

buf_new_fail:
    return error;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        print_usage();

        return 1;
    }

    err_t error = OK;
    err_t close_error = OK;

    char const *url = argv[1];

    sigaction(SIGPIPE, &(struct sigaction) { .sa_handler = SIG_IGN }, NULL);

    int signal_fd = -1;
    int tracked_signals[] = {SIGINT};
    error = ERR(setup_signal_fd(tracked_signals, sizeof(tracked_signals) / sizeof(int), &signal_fd),
        "failed to set up a signal pipe");

    if (ERR_FAILED(error)) goto setup_signal_fd_fail;

    bool should_paginate = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    struct termios previous_settings;

    if (should_paginate) {
        if (ERR_FAILED(error = ERR(wrapper_tcgetattr(STDIN_FILENO, &previous_settings),
                "failed to retrieve tty settings"))) goto tcgetattr_fail;

        struct termios new_settings;
        memcpy(&new_settings, &previous_settings, sizeof(struct termios));

        new_settings.c_lflag &= ~(ICANON | ECHO);
        new_settings.c_cc[VMIN] = 1;
        new_settings.c_cc[VTIME] = 0;

        if (ERR_FAILED(error = ERR(wrapper_tcsetattr(STDIN_FILENO, TCSANOW, &new_settings),
                "failed to configure the tty"))) goto tcsetattr_fail;

        log_printf(LOG_INFO, "Pagination is enabled");
    }

    int sock_fd = -1;
    if (ERR_FAILED(error = ERR(http_get(url, &sock_fd),
            "failed to make an HTTP request"))) goto request_fail;

    if (ERR_FAILED(error = ERR(run_printer(sock_fd, signal_fd, should_paginate), NULL))) {
        goto run_fail;
    }

run_fail:
    if (ERR_FAILED(close_error = ERR(wrapper_close(sock_fd), "failed to close the socket"))) {
        err_log_free(LOG_WARN, &close_error);
    }

request_fail:
    if (should_paginate) {
        close_error = ERR(wrapper_tcsetattr(STDIN_FILENO, TCSANOW, &previous_settings),
            "failed to restore tty settings");

        if (ERR_FAILED(close_error)) {
            err_log_free(LOG_WARN, &close_error);
        }
    }

tcsetattr_fail:
tcgetattr_fail:
    tear_down_signal_fd(signal_fd);

setup_signal_fd_fail:
    if (ERR_FAILED(error)) {
        err_log_free(LOG_ERR, &error);
    }

    return ERR_FAILED(error) ? 1 : 0;
}
