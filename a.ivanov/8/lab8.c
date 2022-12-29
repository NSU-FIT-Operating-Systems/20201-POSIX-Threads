#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <math.h>

#define SUCCESS (0)
#define FAILURE (-1)
#define ITER_COUNT (2000000)
#define ARGC (1 + 1)
#define MAX_THREADS_COUNT (100000)
#define USAGE "./prog <threads count>"

typedef struct coop_info_t {
    int order;
    int threads_count;
    bool created;
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
    for (int i = coop_info.order; i < ITER_COUNT; i += coop_info.threads_count) {
        partial_sum += 1.0 / ( i * 4.0 + 1.0);
        partial_sum -= 1.0 / ( i * 4.0 + 3.0);
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
    }
    if (threads_count > MAX_THREADS_COUNT) {
        fprintf(stderr, "Too many threads\n");
        return FAILURE;
    }
    pthread_t thread_ids[threads_count];
    coop_info_t infos[threads_count];
    for (int i = 0; i < threads_count; i++) {
        infos[i].threads_count = threads_count;
        infos[i].order = i;
        infos[i].created = true;
        int return_code = pthread_create(&thread_ids[i], NULL, calc_partial_sum, &infos[i]);
        if (return_code != SUCCESS) {
            infos[i].created = false;
            fprintf(stderr, "error in pthread_create(): %s\n", strerror(return_code));
        }
    }
    double sum = 0;
    bool failed = false;
    for (int i = 0; i < threads_count; i++) {
        if (!infos[i].created) {
            failed = true;
            continue;
        }
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
