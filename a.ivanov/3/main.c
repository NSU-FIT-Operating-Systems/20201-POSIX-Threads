#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define THREADS_COUNT (4)
#define SUCCESS (0)
#define FAILURE (-1)

typedef struct string_set_t {
    char **strings;
    size_t count;
} string_set_t;

typedef struct thread_maintain_info_t {
    pthread_t id;
    bool was_created;
    void *arg;
} thread_maintain_info_t;

static string_set_t *make_string_set(size_t strings_count) {
    if (strings_count == 0) {
        return NULL;
    }
    string_set_t *set = (string_set_t *) malloc(sizeof(*set));
    if (set == NULL) {
        return NULL;
    }
    char **strings = (char **) malloc((strings_count) * sizeof(*strings));
    if (strings == NULL) {
        return NULL;
    }
    for (size_t j = 1; j <= strings_count; j++) {
        size_t length = j;
        char *str = (char *) malloc(length + 1);
        if (str == NULL) {
            for (size_t k = 0; k < j; k++) {
                free(strings[k]);
            }
            free(strings);
            return NULL;
        }
        str[length] = 0;
        for (size_t k = 0; k < length; k++) {
            str[k] = (char) ('0' + j);
        }
        strings[j - 1] = str;
    }
    set->strings = strings;
    set->count = strings_count;
    return set;
}

static void free_set(string_set_t *set) {
    assert(set);
    for (int i = 0; i < set->count; i++) {
        if (set->strings[i] != NULL) {
            free(set->strings[i]);
        }
    }
    free(set->strings);
    free(set);
}

/*
 * expected type for arg: string_set_t
 * retval is an integer which may be
 * 0 (SUCCESS) or -1 (FAILURE)
 */
static void *print_and_release_string_set(void *arg) {
    string_set_t *string_set = (string_set_t *) arg;
    if (string_set->strings == NULL) {
        pthread_exit((void *) FAILURE);
    }
    for (size_t i = 0; i < string_set->count; i++) {
        if (string_set->strings[i] == NULL) {
            pthread_exit((void *) FAILURE);
        }
        printf("%s ", string_set->strings[i]);
    }
    printf("\n");
    pthread_exit((void *) SUCCESS);
}

int main() {
    thread_maintain_info_t threads[THREADS_COUNT];
    for (size_t i = 0; i < THREADS_COUNT; i++) {
        size_t strings_count = i + 1;
        string_set_t *set = make_string_set(strings_count);
        if (set == NULL) {
            fprintf(stderr, "error in make_string_set()\n");
            continue;
        }
        pthread_t thread_id;
        int return_code = pthread_create(&thread_id, NULL, print_and_release_string_set, set);
        if (return_code != SUCCESS) {
            fprintf(stderr, "error in pthread_create(): %s\n", strerror(return_code));
            threads[i].was_created = false;
            free_set(set);
            continue;
        }
        threads[i].id = thread_id;
        threads[i].was_created = true;
        threads[i].arg = set;
    }
    for (int i = 0; i < THREADS_COUNT; i++) {
        if (!threads[i].was_created) {
            continue;
        }
        void *retval_p;
        int return_code = pthread_join(threads[i].id, (void **) &retval_p);
        if (return_code != SUCCESS) {
            fprintf(stderr, "error in pthread_join(): %s", strerror(return_code));
        } else {
            if ((long) retval_p != SUCCESS) {
                fprintf(stderr, "thread %zu has failed, code: %ld\n", threads[i].id, (long) retval_p);
            }
        }
        free_set(threads[i].arg);
    }
    pthread_exit(NULL);
}
