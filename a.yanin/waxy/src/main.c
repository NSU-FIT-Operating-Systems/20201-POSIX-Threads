#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <common/log/log.h>
#include <common/posix/signal.h>

#include "server.h"

enum {
    CACHE_SIZE = 1024 * 1024 * 1024,
};

static _Atomic(server_t *) server_ref = NULL;

static void on_sigint(int) {
    int saved_errno = errno;
    server_t *server = atomic_exchange(&server_ref, NULL);

    if (server != NULL) {
        server_stop(server);
    }

    errno = saved_errno;
}

static void print_usage(void) {
    // TODO: use the actual name here...
    fputs("Usage: waxy [<port>]\n", stderr);
}

static void set_log_level(void) {
    char const *env = getenv("WAXY_LOG");

    if (env == NULL) {
        env = "info";
    }

    if (strcmp(env, "debug") == 0) {
        log_set_level(LOG_DEBUG);
    } else if (strcmp(env, "info") == 0) {
        log_set_level(LOG_INFO);
    } else if (strcmp(env, "warn") == 0) {
        log_set_level(LOG_WARN);
    } else if (strcmp(env, "err") == 0) {
        log_set_level(LOG_ERR);
    } else if (strcmp(env, "fatal") == 0) {
        log_set_level(LOG_FATAL);
    } else {
        log_set_level(LOG_INFO);
        log_printf(
            LOG_WARN,
            "WAXY_LOG is set to unknown value `%s` (expected `debug`, `info`, `warn`, `err`, or `fatal`)",
            env
        );
        log_printf(LOG_INFO, "Defaulting to INFO");
    }
}

int main(int argc, char **argv) {
    error_t *err = NULL;

    char const *port = NULL;

    if (argc < 2) {
        port = "1234";
    } else if (argc == 2) {
        port = argv[1];
    } else {
        print_usage();

        return 1;
    }

    set_log_level();

    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    err = error_wrap("failed to set the signal mask in the main thread", error_from_errno(
        pthread_sigmask(SIG_BLOCK, &signal_set, NULL)));
    if (err) goto sigmask_block_fail;

    sigaction(SIGPIPE, &(struct sigaction) { .sa_handler = SIG_IGN }, NULL);
    sigaction(SIGINT, &(struct sigaction) { .sa_handler = on_sigint }, NULL);

    log_printf(LOG_INFO, "Starting up...");
    server_t server;
    err = server_new(port, CACHE_SIZE, &server);
    if (err) goto server_new_fail;

    server_ref = &server;

    err = error_wrap("failed to set the signal mask in the main thread", error_from_errno(
        pthread_sigmask(SIG_UNBLOCK, &signal_set, NULL)));
    if (err) goto sigmask_unblock_fail;

    err = server_run(&server);

sigmask_unblock_fail:
    server_ref = NULL;
    server_stop(&server);
    server_await_termination(&server);
    server_free(&server);

server_new_fail:
sigmask_block_fail:
    if (err) {
        error_log_free(&err, LOG_ERR, ERROR_VERBOSITY_SOURCE_CHAIN | ERROR_VERBOSITY_BACKTRACE);
        log_printf(LOG_FATAL, "The server has encountered an error. Exiting.");

        return 1;
    }

    return 0;
}
