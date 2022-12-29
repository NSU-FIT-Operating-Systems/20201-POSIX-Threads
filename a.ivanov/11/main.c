#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SUCCESS (0)
#define LINES_COUNT (10)
#define MUTEX_COUNT (3)

#define ERR_PRINT(name, code) fprintf(stderr, "error in %s(): %s", name, strerror(code))

static pthread_mutex_t mutexes[MUTEX_COUNT];

static bool NEW_STARTED = false;

static void LOCK(size_t id) {
    assert(id >= 0);
    int code = pthread_mutex_lock(&mutexes[id % MUTEX_COUNT]);
    if (code != SUCCESS) {
        ERR_PRINT("pthread_mutex_lock", code);
    }
}

static void UNLOCK(size_t id) {
    assert(id >= 0);
    int code = pthread_mutex_unlock(&mutexes[id % MUTEX_COUNT]);
    if (code != SUCCESS) {
        ERR_PRINT("pthread_mutex_unlock", code);
    }
}

static void *print_lines() {
    /* LLU
     * ULL
     * LUL...
     */
    LOCK(0);
    LOCK(1);
    NEW_STARTED = true;
    for (size_t i = 0; i < LINES_COUNT; i++) {
        printf("%2zu.  new thread line\n", i + 1);
        UNLOCK(i);
        LOCK(i + 2);
    }
    UNLOCK(LINES_COUNT - 1 + 2);
    UNLOCK(LINES_COUNT - 1 + 1);
    return NULL;
}

int main() {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    for (size_t i = 0; i < MUTEX_COUNT; i++) {
        int return_code = pthread_mutex_init(&mutexes[i], &attr);
        if (return_code != SUCCESS) {
            ERR_PRINT("pthread_mutex_init", return_code);
            pthread_exit(NULL);
        }
    }
    pthread_t thread_id;
    int return_code = pthread_create(&thread_id, NULL, print_lines, NULL);
    if (return_code != SUCCESS) {
        ERR_PRINT("pthread_create", return_code);
        pthread_exit(NULL);
    }
    /* LUL
     * LLU
     * ULL...
     */
    LOCK(2);
    while (!NEW_STARTED) {
        usleep(100);
    }
    LOCK(0);
    for (size_t i = 0; i < LINES_COUNT; i++) {
        printf("%2zu.   main line\n", i + 1);
        UNLOCK(i + 2);
        LOCK(i + 1);
    }
    UNLOCK(LINES_COUNT - 1 + 1);
    UNLOCK(LINES_COUNT - 1);
    return_code = pthread_join(thread_id, NULL);
    if (return_code != SUCCESS) {
        ERR_PRINT("pthread_join", return_code);
        pthread_exit(NULL);
    }
    for (size_t i = 0; i < MUTEX_COUNT; i++) {
        return_code = pthread_mutex_destroy(&mutexes[i]);
        if (return_code != SUCCESS) {
            ERR_PRINT("pthread_mutex_destroy", return_code);
            pthread_exit(NULL);
        }
    }
    pthread_exit(NULL);
}
