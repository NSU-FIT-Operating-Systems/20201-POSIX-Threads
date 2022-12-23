#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <semaphore.h>
#include <errno.h>

#define NUMBER_OF_LINES 10

#define NUMBER_OF_SEMAPHORE 2

typedef struct pthread_parameters {
    sem_t semaphores[NUMBER_OF_SEMAPHORE];
}pthread_parameters;

int wait_semaphore(sem_t *sem) {
    if (sem == NULL){
        printf("You've tried to wait for the NULL semaphore!");
        return -1;
    }
    struct timespec ts;
    if(clock_gettime(CLOCK_REALTIME, &ts) == -1){
        printf("Failed to clock_gettime in wait_semaphore!\n");
        return -1;
    }
    ts.tv_sec += 3;
    errno = 0;
    int errorCode = sem_timedwait(sem, &ts);
    if (errorCode == -1) {
        printf("Unable to wait semaphore!");
        return errorCode;
    }
    return 0;
}

int post_semaphore(sem_t *sem) {
    if (sem == NULL){
        printf("You've tried to post the NULL semaphore!");
        return -1;
    }
    int errorCode = sem_post(sem);
    if (-1 == errorCode) {
        printf("Unable to post semaphore!");
        return errorCode;
    }
    return 0;
}

void *print_messages(struct pthread_parameters *parameters, int first_sem, int second_sem, const char *message) {
    if (NULL == message) {
        printf("What am I supposed to print?");
        pthread_exit(0);
    }

    for (int i = 0; i < NUMBER_OF_LINES; i++) {
        if (wait_semaphore(&parameters->semaphores[first_sem]) != 0){
            pthread_exit(0);
        }
        printf("%s", message);
        if (post_semaphore(&parameters->semaphores[second_sem]) != 0){
            pthread_exit(0);
        }
    }
    pthread_exit(0);
}

void *second_print(void *parameters) {
    print_messages(parameters, 1, 0, "Child\n");
    pthread_exit(0);
}

int main() {
    pthread_parameters parameters;

    int errorCode = sem_init(&parameters.semaphores[0], 0, 1);
    if (-1 == errorCode) {
        perror("Unable to init semaphore");
        pthread_exit(0);
    }

    errorCode = sem_init(&parameters.semaphores[1], 0, 0);
    if (-1 == errorCode) {
        perror("Unable to init semaphore");
        sem_destroy(&parameters.semaphores[0]);
        pthread_exit(0);
    }

    pthread_t thread;
    errorCode = pthread_create(&thread, NULL, second_print, &parameters);
    if (0 != errorCode) {
        printf("Unable to create thread!");
        sem_destroy(&parameters.semaphores[0]);
        sem_destroy(&parameters.semaphores[1]);
        pthread_exit(0);
    }

    print_messages(&parameters, 0, 1, "Parent\n");

    errorCode = pthread_join(thread, NULL);
    if (0 != errorCode) {
        printf("Unable to join thread!");
        pthread_exit(0);
    }

    sem_destroy(&parameters.semaphores[0]);
    sem_destroy(&parameters.semaphores[1]);
    pthread_exit(0);
}
