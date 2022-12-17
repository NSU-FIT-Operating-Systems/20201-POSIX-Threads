#include <assert.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>

#define LOG_MODE LOG_MODE_SYNC
#include <common/log/log.h>
#include <common/posix/time.h>

#include "error.h"

enum {
    QUEUE_SIZE = 10,
};

typedef struct {
    sem_t output;
    sem_t available_space;
    char const *part_name;
    struct timespec interval;
} part_spec_t;

typedef struct {
    part_spec_t *a;
    part_spec_t *b;
    part_spec_t *d;
} d_spec_t;

typedef struct {
    part_spec_t *c;
    part_spec_t *d;
} widget_spec_t;

atomic_bool interrupted = false;

static int uninterruptible_wait(sem_t *sem) {
    int ret = -1;

    do {
        errno = 0;
        ret = sem_wait(sem);
    } while (ret == -1 && errno == EINTR);

    return ret;
}

static void produce_part(part_spec_t *part) {
    ERR_ASSERT(ERR((err_errno_t) uninterruptible_wait(&part->available_space),
        "could not wait for an empty output slot"));
    ERR_ASSERT(ERR((err_errno_t) sem_post(&part->output),
        "could not produce a part"));

    log_printf(LOG_INFO, "Produced %s", part->part_name);
}

static void retrieve_part(part_spec_t *part) {
    ERR_ASSERT(ERR((err_errno_t) sem_wait(&part->output),
        "could not retrieve a part"));
    ERR_ASSERT(ERR((err_errno_t) sem_post(&part->available_space),
        "could not free a slot"));

    log_printf(LOG_INFO, "Retrieved %s", part->part_name);
}

static void *run_generic_producer(void *part_spec_opaque) {
    part_spec_t *part_spec = part_spec_opaque;
    char const *part_name = part_spec->part_name;
    struct timespec interval = part_spec->interval;

    while (!interrupted) {
        err_t sleep_err = ERR(wrapper_nanosleep(&interval), NULL);

        if (ERR_FAILED(sleep_err)) {
            err_log_printf_free(LOG_WARN, &sleep_err, "Could not produce %s", part_name);
        }

        produce_part(part_spec);
    }

    return 0;
}

static void *run_d_producer(void *d_spec_opaque) {
    d_spec_t *d_spec = d_spec_opaque;

    while (!interrupted) {
        retrieve_part(d_spec->a);
        retrieve_part(d_spec->b);
        produce_part(d_spec->d);
    }

    return 0;
}

static void *run_widget_producer(void *widget_spec_opaque) {
    widget_spec_t *widget_spec = widget_spec_opaque;

    for (size_t i = 1; !interrupted; ++i) {
        retrieve_part(widget_spec->c);
        retrieve_part(widget_spec->d);
        log_printf(LOG_INFO, "Produced a widget! %zu have been made so far", i);
    }

    return 0;
}

static void print_usage(void) {
    fputs("Usage: semaphactory\n", stderr);
}

static void make_part_spec(
    part_spec_t *spec,
    unsigned int queue_size,
    char const *part_name,
    struct timespec const *interval
) {
    *spec = (part_spec_t) {
        .part_name = part_name,
        .interval = {
            .tv_sec = interval->tv_sec,
            .tv_nsec = interval->tv_nsec,
        },
    };

    ERR_ASSERT(ERR((err_errno_t) sem_init(&spec->output, 0, 0),
        "could not create an output semaphore"));
    ERR_ASSERT(ERR((err_errno_t) sem_init(&spec->available_space, 0, queue_size),
        "could not create an available_space semaphore"));
}

static void free_part_spec(part_spec_t *spec) {
    sem_destroy(&spec->output);
    sem_destroy(&spec->available_space);
}

int main(int argc, char *[]) {
    if (argc != 1) {
        print_usage();

        return 1;
    }

    err_t error = OK;

    part_spec_t a;
    part_spec_t b;
    part_spec_t c;
    part_spec_t d;

    make_part_spec(&a, QUEUE_SIZE, "part A", &(struct timespec) { .tv_sec = 1 });
    make_part_spec(&b, QUEUE_SIZE, "part B", &(struct timespec) { .tv_sec = 2 });
    make_part_spec(&c, QUEUE_SIZE, "part C", &(struct timespec) { .tv_sec = 3 });
    make_part_spec(&d, QUEUE_SIZE, "a module", &(struct timespec) { 0 });

    d_spec_t d_spec = {
        .a = &a,
        .b = &b,
        .d = &d,
    };

    widget_spec_t widget_spec = {
        .c = &c,
        .d = &d,
    };

    void *(*producers[5])(void *) = {
        run_generic_producer,
        run_generic_producer,
        run_generic_producer,
        run_d_producer,
        run_widget_producer,
    };

    void *params[5] = { &a, &b, &c, &d_spec, &widget_spec };
    pthread_t threads[5] = { 0 };

    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    ERR_ASSERT(ERR((err_errno_t) pthread_sigmask(SIG_BLOCK, &signal_set, NULL),
        "failed to block signals in the main thread"));

    pthread_t *last_thread_id = &threads[0];

    for (size_t i = 0; i < 5; ++i, ++last_thread_id) {
        error = ERR((err_errno_t) pthread_create(last_thread_id, NULL, producers[i], params[i]),
            "could not create a producer");
        if (ERR_FAILED(error)) goto pthread_create_fail;
    }

    log_printf(LOG_INFO, "Press ^C to terminate...");

    {
        int sig;
        sigwait(&signal_set, &sig);
    }

    log_printf(LOG_WARN, "Finishing up, please wait...");

pthread_create_fail:
    interrupted = true;

    for (pthread_t *thread_id = &threads[0]; thread_id != last_thread_id; ++thread_id) {
        err_t join_error = ERR((err_errno_t) pthread_join(*thread_id, NULL),
            "could not join a child thread");

        if (ERR_FAILED(join_error)) {
            err_log_free(LOG_ERR, &join_error);
        }
    }

    free_part_spec(&a);
    free_part_spec(&b);
    free_part_spec(&c);
    free_part_spec(&d);

    if (ERR_FAILED(error)) {
        err_log_free(LOG_ERR, &error);

        return 1;
    }

    pthread_exit(0);
}
