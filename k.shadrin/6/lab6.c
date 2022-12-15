#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

int all_inited = 0;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void *thread_start_routine(void *params) {
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
    usleep(strlen(string) * 100000);
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
    printf("%d\n", argc - 1);
    for(int i = 1; i < argc; ++i){
        err = pthread_create(&threads[i - 1], NULL, thread_start_routine, (void *) argv[i]);
        if (err != 0) {
            printf("pthread_create error: ");
            pthread_exit(0);
        }
    }
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

    err = pthread_cond_destroy(&cond);
    if(err != 0){
        printf("pthread_cond_destroy error!");
        pthread_exit(0);
    }
    
    err = pthread_mutex_unlock(&mutex);
    if(err != 0){
        printf("pthread_mutex_unlock error!");
        pthread_exit(0);
    }
    pthread_exit(0);
}
