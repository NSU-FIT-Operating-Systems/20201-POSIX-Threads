#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define STEPS 200000000

long thread_count = 0;
int all_inited = 0;
long inited_count = 0;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct Thread_params{
    long id; // Идентификатор потока(нужен для оффсета)
    double psum; // Частичная сумма потока
} thread_params;

// Подсчёт частичной суммы числа пи
void *calculate(void *param) {
    if(param == NULL){
        printf("Invalid parameters at the calculate function!\n");
        return NULL;
    }
    inited_count++;
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
    thread_params *params =(thread_params*)param;
    if(params == NULL){
        printf("Invalid parameters at the calculate function!\n");
        return params;
    }
    double partial_sum = 0.0;
    long long id = params->id;
    for(long long i = id; i < STEPS; i += thread_count) { // по смещению работаем
        partial_sum += 1.0 / (i * 4.0 + 1.0);
        partial_sum -= 1.0 / (i * 4.0 + 3.0);
    }
    params->psum = partial_sum;
    printf("Thread %lld: sum == %.16f\n", id, params->psum);
    return params;
}

// Утилита для быстрого создания потоков и инициализации их параметров
void create_threads(pthread_t *threads, thread_params *params, long requested_num){
    if(params == NULL || threads == NULL){
        printf("Invalid parameters at the create_thread function!\n");
        return;
    }
    int err;
    for(long i = 0; i < requested_num; i++){
        params[i].id = i;
        // Даём каждому потоку задачу вычислить частичную сумму и параметры впридачу
        err = pthread_create(&threads[i], NULL, calculate, &params[i]);
        if(err != 0){
            printf("Couldn't create thread!\n");
            break;
        }
        thread_count++;
    }
}

// Утилита для применения pthread_join() для всех созданных и работающих потоков плюс получение вычисленного ими значения числа пи
int threads_join(pthread_t *threads, double *pi){
    if(pi == NULL || threads == NULL){
        printf("Invalid parameters at the threads_join function!\n");
        return 0;
    }
    int err;
    int retval = 0;
    double sum = 0.0;
    for(long i = 0; i < thread_count; ++i){
        thread_params *res = NULL;
        err = pthread_join(threads[i], (void **)&res);
        if(err != 0){
            printf("Failed to join thread!\n");
            retval = -1;
            continue;
        }
        if(NULL == res) {
            printf("Thread has returned NOTHING\n");
            retval = -1;
            continue;
        }
        sum += res->psum;
    }
    (*pi) = sum * 4;

    return retval;
}


int main(int argc, char ** argv) {
    if(argc != 2){
        printf("Usage: [executable_name] + [number_of_threads]\n");
        return 0;
    }

    long requested_num;
    char *tc_string = argv[1];
    errno = 0;
    int err;
    char *end = "";
    requested_num = strtol(tc_string, &end, 10); // конвертируем argv значение в чиселку
    if(errno != 0){
        perror("Can't convert given number!\n");
        pthread_exit(0);
    }
    if(requested_num < 1){
        printf("You can't create 0 or less threads!\n");
        pthread_exit(0);
    }

    pthread_t threads[requested_num];
    thread_params params[requested_num];

    // Создание и инициализация потоков с параметрами
    create_threads(threads, params, requested_num);
    while(inited_count != thread_count){
        sleep(1);
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

    if(thread_count < requested_num){
        printf("Somehow we couldn't create as many threads as you wished!\nSo now we are working with %ld threads!\n", thread_count);
    }

    // Основное действо
    double result = 0.0;
    if(0 != threads_join(threads, &result)){
        printf("Couldn't calculate PI!\n");
        pthread_exit(0);
    }

    printf("Result: pi = %.16f\n", result);

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
