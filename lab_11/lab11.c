#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NUMBER_OF_LINES 10

#define TRUE 1
#define FALSE 0

pthread_mutex_t mutexes[3];
int child_thread_locked_mutex = FALSE;

//блокировка мутекса
void lock_mutex(int mutex_num) {
    int error_code = pthread_mutex_lock(&mutexes[mutex_num]);
    if (error_code != 0) {
        printf("Unable to lock mutex %d", mutex_num);
    }
}

//разблокировка мутекса
void unlock_mutex(int mutex_num) {
    int error_code = pthread_mutex_unlock(&mutexes[mutex_num]);
    if (error_code != 0) {
        printf("Unable to unlock mutex %d", mutex_num);
    }
}

//дочерний процесс печатает
void *child_print(void *param) {
    lock_mutex(2);
    child_thread_locked_mutex = TRUE;
    for (int i = 0; i < NUMBER_OF_LINES; i++) {
        lock_mutex(1);
        printf("Child\n");
        unlock_mutex(2);
        lock_mutex(0);
        unlock_mutex(1);
        lock_mutex(2);
        unlock_mutex(0);
    }
    unlock_mutex(2);
    return NULL;
}

//родительский процес печатает
void parent_print() {
    for (int i = 0; i < NUMBER_OF_LINES; i++) {
        printf("Parent\n");
        lock_mutex(0);
        unlock_mutex(1);
        lock_mutex(2);
        unlock_mutex(0);
        lock_mutex(1);
        unlock_mutex(2);
    }
}

// уничтожение мутексов
void destroy_mutexes(int count) {
    for (int i = 0; i < count; i++) {
        pthread_mutex_destroy(&mutexes[i]);
    }
}

// инициализация мутексов
int init_mutexes() {
    pthread_mutexattr_t mutex_attrs;
    int error_code = pthread_mutexattr_init(&mutex_attrs);
    if (error_code != 0) {
        perror("Unable to init mutex attrs");
        return -1;
    }

    error_code = pthread_mutexattr_settype(&mutex_attrs, PTHREAD_MUTEX_ERRORCHECK);
    if (error_code != 0) {
        perror("Unable to init mutex attrs type");
        pthread_mutexattr_destroy(&mutex_attrs);
        return -1;
    }

    for (int i = 0; i < 3; i++) {
        error_code = pthread_mutex_init(&mutexes[i], &mutex_attrs);
        if (error_code != 0) {
            pthread_mutexattr_destroy(&mutex_attrs);
            destroy_mutexes(i);
            return -1;
        }
    }

    pthread_mutexattr_destroy(&mutex_attrs);
    return 0;
}

int main() {
    int error_code = init_mutexes();
    if (error_code != 0) {
        return EXIT_FAILURE;
    }

    lock_mutex(1);

    pthread_t thread;
    error_code = pthread_create(&thread, NULL, child_print, NULL);
    if (error_code != 0) {
        perror("Unable to create thread");
        unlock_mutex(1);
        destroy_mutexes(3);
        return EXIT_FAILURE;
    }

    while (!child_thread_locked_mutex) { // Ждём инициализации
        sleep(1);
    }

    parent_print();
    unlock_mutex(1);

    destroy_mutexes(3);
    pthread_exit(NULL);
}