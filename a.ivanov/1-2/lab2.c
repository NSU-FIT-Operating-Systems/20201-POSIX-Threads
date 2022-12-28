#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define SUCCESS (0)
#define FAILURE (-1)
#define LINES_COUNT (10)

static void *print_lines() {
    for (int i = 0; i < LINES_COUNT; i++) {
        printf("%2d.  new thread line\n", i + 1);
    }
    return NULL;
}

int main() {
    pthread_t thread_id;
    int return_code = pthread_create(&thread_id, NULL, print_lines, NULL);
    if (return_code != SUCCESS) {
        fprintf(stderr, "failure in pthread_create(): %s", strerror(return_code));
        return FAILURE;
    }
    return_code = pthread_join(thread_id, NULL);
    if (return_code != SUCCESS) {
        fprintf(stderr, "failure in pthread_join(): %s", strerror(return_code));
        pthread_exit(NULL);
    }
    for (int i = 0; i < LINES_COUNT; i++) {
        printf("%2d. main thread line\n", i + 1);
    }
    pthread_exit(NULL);
}
