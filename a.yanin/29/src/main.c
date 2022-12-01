#include <assert.h>
#include <string.h>

#include <common/log/log.h>
#include <common/posix/aio.h>
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
    MIN_READ_SIZE = 1024 * 1024,
    LINE_LIMIT = 25,
};

static void print_usage(void) {
    fputs("Usage: http-aio <url>\n", stderr);
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

typedef struct {
    struct aiocb cb;
    struct aiocb const **await_entry;
    int8_t signal;
    int fd;
} signal_data_t;

typedef struct {
    struct aiocb cb;
    struct aiocb const **await_entry;
    char buf[BUFSIZ];
    int fd;
} stdin_data_t;

typedef struct {
    struct aiocb cb;
    struct aiocb const **await_entry;
    buf_t *response;
    int fd;
} sock_data_t;

static void signal_data_new(
    int signal_fd,
    struct aiocb const **await_entry,
    signal_data_t *result
) {
    assert(signal_fd >= 0);
    assert(await_entry != NULL);
    assert(result != NULL);

    *result = (signal_data_t) {
        .await_entry = await_entry,
        .signal = -1,
        .fd = signal_fd,
    };
}

static void stdin_data_new(
    int stdin_fd,
    struct aiocb const **await_entry,
    stdin_data_t *result
) {
    assert(stdin_fd >= 0);
    assert(await_entry != NULL);
    assert(result != NULL);

    *result = (stdin_data_t) {
        .await_entry = await_entry,
        .buf = {'\0'},
        .fd = stdin_fd,
    };
}

static void sock_data_new(
    int sock_fd,
    struct aiocb const **await_entry,
    buf_t *response,
    sock_data_t *result
) {
    assert(sock_fd >= 0);
    assert(await_entry != NULL);
    assert(response != NULL);
    assert(result != NULL);

    *result = (sock_data_t) {
        .await_entry = await_entry,
        .response = response,
        .fd = sock_fd,
    };
}

static err_t signal_data_request(signal_data_t *self) {
    assert(self != NULL);

    memset(&self->cb, 0, sizeof(struct aiocb));
    self->cb.aio_fildes = self->fd;
    self->cb.aio_offset = 0;
    self->cb.aio_buf = &self->signal;
    self->cb.aio_nbytes = 1;
    self->cb.aio_reqprio = 0;
    self->cb.aio_sigevent.sigev_notify = SIGEV_NONE;
    self->cb.aio_lio_opcode = LIO_READ;

    err_t error = ERR(wrapper_aio_read(&self->cb), "failed to post an aio request");
    *self->await_entry = ERR_FAILED(error) ? NULL : &self->cb;

    return error;
}

static err_t stdin_data_request(stdin_data_t *self) {
    assert(self != NULL);

    memset(&self->cb, 0, sizeof(struct aiocb));
    self->cb.aio_fildes = self->fd;
    self->cb.aio_offset = 0;
    self->cb.aio_buf = self->buf;
    self->cb.aio_nbytes = BUFSIZ;
    self->cb.aio_reqprio = 0;
    self->cb.aio_sigevent.sigev_notify = SIGEV_NONE;
    self->cb.aio_lio_opcode = LIO_READ;

    err_t error = ERR(wrapper_aio_read(&self->cb), "failed to post an aio request");
    *self->await_entry = ERR_FAILED(error) ? NULL : &self->cb;

    return error;
}

static err_t sock_data_request(sock_data_t *self) {
    assert(self != NULL);

    err_t error = OK;

    if (buf_immediately_available_write_size(self->response) < MIN_READ_SIZE) {
        error = ERR(buf_ensure_enough_write_space(self->response, INITIAL_BUF_SIZE), NULL);

        if (ERR_FAILED(error)) return error;
    }

    memset(&self->cb, 0, sizeof(struct aiocb));
    self->cb.aio_fildes = self->fd;
    self->cb.aio_offset = 0;
    self->cb.aio_buf = buf_get_write_ptr(self->response);
    self->cb.aio_nbytes = buf_immediately_available_write_size(self->response);
    self->cb.aio_reqprio = 0;
    self->cb.aio_sigevent.sigev_notify = SIGEV_NONE;
    self->cb.aio_lio_opcode = LIO_READ;

    error = ERR(wrapper_aio_read(&self->cb), "failed to post an aio request");
    *self->await_entry = ERR_FAILED(error) ? NULL : &self->cb;

    return error;
}

static err_t finalize_async_read(struct aiocb *cb, bool *in_progress, ssize_t *read_count) {
    assert(cb != NULL);
    assert(in_progress != NULL);

    *in_progress = false;

    err_t error = OK;

    posix_err_t status = wrapper_aio_error(cb);

    if (status.errno_code == EINPROGRESS) {
        *in_progress = true;

        return error;
    }

    if (ERR_FAILED(error = ERR(status, "the read operation failed"))) return error;

    return ERR(wrapper_aio_return(cb, read_count),
        "failed to finalize reading from the signal pipe");
}

static err_t signal_data_process(signal_data_t *self, bool *running) {
    assert(self != NULL);
    assert(running != NULL);

    err_t error = OK;

    if (*self->await_entry == NULL) {
        return error;
    }

    ssize_t read_count = -1;
    bool in_progress = false;

    if (ERR_FAILED(error = ERR(finalize_async_read(&self->cb, &in_progress, &read_count),
            "reading from the signal pipe failed"))) {
        return error;
    } else if (in_progress) {
        return OK;
    } else if (ERR_FAILED(error = ERR((bool)(read_count != 0),
            "the signal pipe was closed unexpectedly"))) {
        return error;
    }

    if (self->signal == SIGINT) {
        *running = false;
    }

    return ERR(signal_data_request(self), "failed to post a read request");
}

static void stop_waiting_for_user(bool *waiting_for_user) {
    *waiting_for_user = false;
    fputs("\r\x1b[K", stderr);
    fflush(stderr);
}

static err_t stdin_data_process(
    stdin_data_t *self,
    bool *should_paginate,
    bool *waiting_for_user,
    bool waiting_for_server
) {
    assert(self != NULL);
    assert(should_paginate != NULL);
    assert(waiting_for_user != NULL);

    err_t error = OK;

    if (*self->await_entry == NULL) {
        return error;
    }

    ssize_t read_count = -1;
    bool in_progress = false;

    if (ERR_FAILED(error = ERR(finalize_async_read(&self->cb, &in_progress, &read_count),
            "reading from stdin failed"))) {
        return error;
    } else if (in_progress) {
        return OK;
    }

    if (read_count == 0) {
        *should_paginate = false;
        stop_waiting_for_user(waiting_for_user);
        *self->await_entry = NULL;

        return error;
    }

    if (memchr(self->buf, ' ', read_count) != NULL) {
        if (*waiting_for_user) {
            stop_waiting_for_user(waiting_for_user);
        } else if (waiting_for_server) {
            log_printf(LOG_INFO, "The server is too slow, please be patient...");
        }
    }

    return ERR(stdin_data_request(self), "failed to post a read request");
}

static err_t sock_data_process(sock_data_t *self, bool *eof) {
    assert(self != NULL);
    assert(eof != NULL);

    err_t error = OK;

    if (*self->await_entry == NULL) {
        return error;
    }

    ssize_t read_count = -1;
    bool in_progress = false;

    if (ERR_FAILED(error = ERR(finalize_async_read(&self->cb, &in_progress, &read_count),
            "reading from the remote host failed"))) {
        return error;
    } else if (in_progress) {
        return OK;
    }

    if (ERR_FAILED(error = ERR(buf_write(self->response, NULL, read_count),
            "failed to commit received data to the buffer"))) {
        return error;
    }

    if (read_count == 0) {
        *eof = true;
        *self->await_entry = NULL;

        return error;
    }

    return ERR(sock_data_request(self), "failed to post a read request");
}

static err_t print_lines(buf_t *response, size_t *line_count) {
    assert(response != NULL);
    assert(line_count != NULL);

    err_t error = OK;

    size_t byte_count = buf_available_read_size(response);
    char const *start = buf_get_read_ptr(response);
    char const *last_line = start;
    char const *end = start + byte_count;

    for (; last_line < end && *line_count < LINE_LIMIT; ++(*line_count)) {
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

static bool has_full_page(buf_t *response) {
    size_t line_count = 0;
    size_t byte_count = buf_available_read_size(response);
    char const *p = buf_get_read_ptr(response);
    char const *end = p + byte_count;

    for (; p < end && line_count < LINE_LIMIT; ++line_count) {
        p = memchr(p, '\n', end - p - 1);

        if (p == NULL) {
            return false;
        }

        ++p;
    }

    return line_count >= LINE_LIMIT;
}

static err_t run_printer(int sock_fd, int signal_fd, bool should_paginate) {
    assert(sock_fd >= 0);
    assert(signal_fd >= 0);

    err_t error = OK;

    size_t line_count = 0;
    bool waiting_for_user = false;
    // true if we aren't printing anything because we're waiting for the server to send us a
    // full page (25 lines) and not because the user is too slow
    bool waiting_for_server = false;

    bool running = true;
    bool eof = false;

    buf_t response;

    if (ERR_FAILED(error = ERR(buf_new(INITIAL_BUF_SIZE, false, &response),
            "failed to allocate the buffer"))) goto buf_new_fail;

    struct aiocb const *cbs[3] = {NULL, NULL, NULL};
    size_t cb_count = sizeof(cbs) / sizeof(cbs[0]);

    struct aiocb const **signal_await_entry = &cbs[0];
    struct aiocb const **sock_await_entry = &cbs[1];
    struct aiocb const **stdin_await_entry = &cbs[2];

    signal_data_t signal_data;
    sock_data_t sock_data;
    stdin_data_t stdin_data;

    signal_data_new(signal_fd, signal_await_entry, &signal_data);
    sock_data_new(sock_fd, sock_await_entry, &response, &sock_data);
    stdin_data_new(STDIN_FILENO, stdin_await_entry, &stdin_data);

    if (ERR_FAILED(error = ERR(signal_data_request(&signal_data), NULL))) {
        goto handle_fail;
    }

    if (should_paginate) {
        if (ERR_FAILED(error = ERR(stdin_data_request(&stdin_data), NULL))) {
            goto handle_fail;
        }
    }

    if (ERR_FAILED(error = ERR(sock_data_request(&sock_data), NULL))) {
        goto handle_fail;
    }

    while (running) {
        if (ERR_FAILED(error = ERR(wrapper_aio_suspend(cbs, cb_count, NULL),
                NULL))) goto suspend_fail;

        if (ERR_FAILED(error = ERR(signal_data_process(&signal_data, &running),
                NULL))) goto handle_fail;
        if (ERR_FAILED(error = ERR(sock_data_process(&sock_data, &eof), NULL))) goto handle_fail;

        waiting_for_server = (
            !waiting_for_user &&
            // don't paginate the output if we shouldn't and just print everything right away
            should_paginate &&
            // if the server's telling us it won't send anything anymore, there's no point in
            // waiting
            !eof &&
            !has_full_page(&response)
        );

        if (ERR_FAILED(error = ERR(stdin_data_process(&stdin_data, &should_paginate,
                &waiting_for_user, waiting_for_server), NULL))) goto handle_fail;

        if (should_paginate) {
            if (!waiting_for_user && !waiting_for_server) {
                print_lines(&response, &line_count);
            }
        } else {
            char const *buf = buf_get_read_ptr(&response);
            size_t count = buf_available_read_size(&response);
            error = ERR(write_all(STDOUT_FILENO, (unsigned char const *) buf, count),
                "failed to write the received data");
            if (ERR_FAILED(error)) goto handle_fail;
            if (ERR_FAILED(error = buf_read(&response, NULL, count))) goto handle_fail;
        }

        if (eof && buf_available_read_size(&response) == 0) {
            break;
        }

        if (should_paginate && line_count == LINE_LIMIT) {
            waiting_for_user = true;
            line_count = 0;
            fputs("\x1b[36mPress space to continue...\x1b[m", stderr);
            fflush(stderr);
        }
    }

suspend_fail:
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
        log_printf(LOG_INFO, "Pagination is enabled");
        if (ERR_FAILED(error = ERR(wrapper_tcgetattr(STDIN_FILENO, &previous_settings),
                "failed to retrieve tty settings"))) goto tcgetattr_fail;

        struct termios new_settings;
        memcpy(&new_settings, &previous_settings, sizeof(struct termios));

        new_settings.c_lflag &= ~(ICANON | ECHO);
        new_settings.c_cc[VMIN] = 1;
        new_settings.c_cc[VTIME] = 0;

        if (ERR_FAILED(error = ERR(wrapper_tcsetattr(STDIN_FILENO, TCSANOW, &new_settings),
                "failed to configure the tty"))) goto tcsetattr_fail;
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
