#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>

int all_inited = 0;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
atomic_int count = 0;

void *thread_start_routine(void *params) {
    ++count;
    int err;
    err = pthread_mutex_lock(&mutex);
    if(err != 0){
        printf("pthread_mutex_lock error!");
        pthread_exit(0);
    }
    while (!all_inited)
    {
        err = pthread_cond_wait(&cond, &mutex);
        if(err != 0){
            printf("pthread_cond_wait error!");
            pthread_exit(0);
        }
    }
    err = pthread_mutex_unlock(&mutex);
    if(err != 0){
        printf("pthread_mutex_unlock error!");
        pthread_exit(0);
    }
    char *string = (char*)params;
    usleep(strlen(string) * 12500);
    printf("%s\n", string);
    pthread_exit(0);
}

int main(int argc, char *argv[]) {
    if(argc < 2 || argc > 101){
        printf("Too many or too few args!\n");
        return -1;
    }
    pthread_t threads[100];
    int err;
    int thread_num = 0;
    for(int i = 1; i < argc; ++i){
        err = pthread_create(&threads[i - 1], NULL, thread_start_routine, (void *) argv[i]);
        if (err != 0) {
            printf("pthread_create error: ");
            pthread_exit(0);
        }
        thread_num++;
    }

    while(count != thread_num){
        sleep(1);
    }
    
    printf("Sort started!\n");
    err = pthread_mutex_lock(&mutex);
    if(err != 0){
        printf("pthread_mutex_lock error!");
        pthread_exit(0);
    }
    
    all_inited = 1;
    err = pthread_cond_broadcast(&cond);
    if(err != 0){
        printf("pthread_cond_broadcast error!");
        pthread_exit(0);
    }

    err = pthread_mutex_unlock(&mutex);
    if(err != 0){
        printf("pthread_mutex_unlock error!");
        pthread_exit(0);
    }

    for(int i = 0; i < thread_num; ++i){
        err = pthread_join(threads[i], NULL);
        if(err != 0){
            printf("Join error!");
            pthread_mutex_destroy(&mutex);
            pthread_cond_destroy(&cond);
            pthread_exit(0);
        }
    }

    err = pthread_cond_destroy(&cond);
    if(err != 0){
        printf("pthread_cond_destroy error!");
        pthread_exit(0);
    }
    
    err = pthread_mutex_destroy(&mutex);
    if(err != 0){
        printf("pthread_mutex_destroy error!");
        pthread_exit(0);
    }
    pthread_exit(0);
}
