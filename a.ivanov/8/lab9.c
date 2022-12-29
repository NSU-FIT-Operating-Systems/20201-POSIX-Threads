#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <math.h>

#define SUCCESS (0)
#define FAILURE (-1)
#define ITER_CHECK_COUNT (1000000)
#define MAX_THREADS_COUNT (100000)
#define ARGC (1 + 1)
#define USAGE "./prog <threads count>"

static bool SIGINT_WAS = false;

static void sigint_handler(int sig) {
    SIGINT_WAS = true;
}

typedef struct coop_info_t {
    int order;
    int threads_count;
} coop_info_t;

static bool extract_int(const char *buf, int *num) {
    if (NULL == buf || num == NULL) {
        return false;
    }
    char *end_ptr = NULL;
    *num = (int) strtol(buf, &end_ptr, 10);
    if (buf + strlen(buf) != end_ptr) {
        return false;
    }
    return true;
}

static void *calc_partial_sum(void *arg) {
    coop_info_t coop_info = *((coop_info_t *) arg);
    double *partial_sum_ptr = (double *) malloc(sizeof(*partial_sum_ptr));
    if (NULL == partial_sum_ptr) {
        return NULL;
    }
    double partial_sum = 0;
    int count = 0;
    bool warned = false;
    size_t iter_check_count = ITER_CHECK_COUNT / coop_info.threads_count;
    for (long i = coop_info.order; true; i += coop_info.threads_count) {
        double denominator_main_part = (double) (i << 2);
        if (denominator_main_part < 0) {
            break;
        }
        double term = 1.0 / ( denominator_main_part + 1.0) - 1.0 / ( denominator_main_part + 3.0);
        partial_sum += term;
        count++;
        if (count == iter_check_count) {
            count = 0;
            if (warned) {
                break;
            } else if (SIGINT_WAS) {
                warned = true;
            }
        }
    }
    *partial_sum_ptr = partial_sum;
    return partial_sum_ptr;
}

int main(int argc, char *argv[]) {
    if (argc != ARGC) {
        fprintf(stderr, "%s\n", USAGE);
        return FAILURE;
    }
    int threads_count;
    bool extracted = extract_int(argv[1], &threads_count);
    if (!extracted) {
        fprintf(stderr, "%s\n", USAGE);
        return FAILURE;
    }
    if (threads_count <= 0) {
        fprintf(stderr, "threads count must be positive\n");
        return FAILURE;
    } else if (threads_count >= MAX_THREADS_COUNT) {
        fprintf(stderr, "Too many threads\n");
        return FAILURE;
    }
    signal(SIGINT, sigint_handler);
    pthread_t thread_ids[threads_count];
    coop_info_t infos[threads_count];
    for (int i = 0; i < threads_count; i++) {
        infos[i].threads_count = threads_count;
        infos[i].order = i;
        int return_code = pthread_create(&thread_ids[i], NULL, calc_partial_sum, &infos[i]);
        if (return_code != SUCCESS) {
            fprintf(stderr, "error in pthread_create(): %s\n", strerror(return_code));
        }
    }
    double sum = 0;
    bool failed = false;
    for (int i = 0; i < threads_count; i++) {
        void *ret_val;
        int return_code = pthread_join(thread_ids[i], &ret_val);
        if (return_code != SUCCESS) {
            fprintf(stderr, "error in pthread_join(): %s\n", strerror(return_code));
            failed = true;
            continue;
        }
        if (ret_val == NULL) {
            fprintf(stderr, "thread %lu failed\n", thread_ids[i]);
            failed = true;
        } else {
            sum += *((double *) ret_val);
            free(ret_val);
        }
    }
    if (failed) {
        fprintf(stderr, "could not calculate PI due to errors\n");
        pthread_exit(NULL);
    }
    double pi = sum * 4.0;
    printf("\n");
    printf("PI = %.15g\n\n", pi);
    printf("accuracy = %.15g\n", M_PI - pi);
    pthread_exit(NULL);
}
