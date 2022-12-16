#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define NUMBER_OF_LINES 10

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int cur_printing_thread = 0;

// обёртка для pthread_mutex_lock()
int lock_mutex(pthread_mutex_t *mutex_lock) {
    if (mutex_lock == NULL){
        printf("You've tried to lock the NULL mutex_lock!");
        return -1;
    }
    int errorCode = pthread_mutex_lock(mutex_lock);
    if (errorCode != 0) {
        printf("Unable to lock mutex_lock!");
        return errorCode;
    }
    return 0;
}

// обёртка для pthread_mutex_unlock()
int unlock_mutex(pthread_mutex_t *mutex_unlock) {
    if (mutex_unlock == NULL){
        printf("You've tried to lock the NULL mutex!");
        return -1;
    }
    int errorCode = pthread_mutex_unlock(mutex_unlock);
    if (errorCode != 0) {
        printf("Unable to unlock mutex!");
        return errorCode;
    }
    return 0;
}

// обёртка для pthread_cond_wait()
int cond_wait(pthread_cond_t *cond_wait, pthread_mutex_t *mutex_wait) {
    if (mutex_wait == NULL){
        printf("You've tried to wait with NULL mutex!");
        return -1;
    }
    if(cond_wait == NULL){
        printf("You've tried to wait for the NULL conditional variable!");
        return -1;
    }
    int errorCode = pthread_cond_wait(cond_wait, mutex_wait);
    if (errorCode != 0) {
        printf("Unable to wait cond variable!");
        return errorCode;
    }
    return 0;
}

// обёртка для pthread_cond_signal()
int cond_signal(pthread_cond_t *cond_sig) {
    if (NULL == cond_sig){
        printf("You've tried to send to NULL condition variable!");
        return -1;
    }
    int errorCode = pthread_cond_signal(cond_sig);
    if (errorCode != 0) {
        printf("Unable to signal cond!");
        return errorCode;
    }
    return 0;
}

// а так мы печатаем)
void *print_messages(const char *message, int calling_thread) {
    if (message == NULL) {
        printf("What am I supposed to print!");
        pthread_exit(0);
    }

    for (int i = 0; i < NUMBER_OF_LINES; i++) {
        if (lock_mutex(&mutex) != 0){
            pthread_exit(0);
        }
        while (cur_printing_thread != calling_thread) {
            if (cond_wait(&cond, &mutex) != 0){
                pthread_exit(0);
            }
        }
        printf("%s", message);
        if (cur_printing_thread == 0) {
            cur_printing_thread = 1;
        } else {
            cur_printing_thread = 0;
        }
        if (cond_signal(&cond) != 0){
            pthread_exit(0);
        }
        if (unlock_mutex(&mutex) != 0){
            pthread_exit(0);
        }
    }

}

void *second_print(void *param) {
    print_messages("Child\n", 1);
    pthread_exit(0);
}


int main() {
    pthread_t thread;
    int errorCode = pthread_create(&thread, NULL, second_print, NULL);
    if (0 != errorCode) {
        printf("Unable to create thread!");
        pthread_mutex_destroy(&mutex);
        pthread_cond_destroy(&cond);
        return EXIT_FAILURE;
    }

    print_messages("Parent\n", 0);

    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    pthread_exit(0);
}
