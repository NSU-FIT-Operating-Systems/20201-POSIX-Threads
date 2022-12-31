#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define LINES_COUNT (50)
#define SECOND (1)
#define SUCCESS (0)
#define UNTIL_CANCEL_TIME (4)

pid_t gettid();

static void thread_cleanup(void *arg) {
    printf("[INFO] thread %d was cancelled", gettid());
}

static void *long_print(void *arg) {
    pthread_cleanup_push(thread_cleanup, NULL);
    for (size_t i = 0; i < LINES_COUNT; i++) {
        sleep(SECOND);
        printf("one more second passed\n");
    }
    printf("dsgdgdfgdfh\n");
    pthread_cleanup_pop(NULL);
    pthread_exit(NULL);
}

int main() {
    pthread_t thread_id;
    int return_code = pthread_create(&thread_id, NULL, long_print, NULL);
    if (return_code != SUCCESS) {
        fprintf(stderr, "error in pthread_create(): %s\n", strerror(return_code));
        pthread_exit(NULL);
    }
    sleep(UNTIL_CANCEL_TIME);
    return_code = pthread_cancel(thread_id);
    if (return_code != SUCCESS) {
        fprintf(stderr, "error in pthread_cancel(): %s\n", strerror(return_code));
        pthread_exit(NULL);
    }
    // we do join in order to ensure that finish of a cancellation occurred
    return_code = pthread_join(thread_id, NULL);
    if (return_code != SUCCESS) {
        fprintf(stderr, "error in pthread_join(): %s\n", strerror(return_code));
        pthread_exit(NULL);
    }
    pthread_exit(NULL);
}
