#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#define STEPS 200000000

typedef struct Thread_params{
    long thread_count; // Количество thread-ов
    long long id; // Идентификатор потока(нужен для оффсета)
    double psum; // Частичная сумма потока
} thread_params;

// Подсчёт частичной суммы числа пи
void *calculate(void *param) {
    if(param == NULL){
        printf("Invalid parameters at the calculate function!\n");
        return NULL;
    }
    thread_params *params =(thread_params*)param;
    if(params == NULL){
        printf("Invalid parameters at the calculate function!\n");
        return params;
    }
    double partial_sum = 0.0;
    long long id = params->id;
    long thread_count = params->thread_count;
    for(long long i = id; i < STEPS; i += thread_count) { // по смещению работаем
        partial_sum += 1.0 / (i * 4.0 + 1.0);
        partial_sum -= 1.0 / (i * 4.0 + 3.0);
    }
    params->psum = partial_sum;
    printf("Thread %lld: sum == %.16f\n", id, params->psum);
    return params;
}

// Утилита для быстрого создания потоков и инициализации их параметров
long create_threads(pthread_t *threads, thread_params *params, long thread_count){
    if(thread_count < 1 || params == NULL || threads == NULL){
        printf("Invalid parameters at the create_thread function!\n");
        return 0;
    }
    int err;
    long init_count = 0;
    for(long i = 0; i < thread_count; ++i){
        params[i].thread_count = thread_count;
        params[i].id = i;
        // Даём каждому потоку задачу вычислить частичную сумму и параметры впридачу
        err = pthread_create(&threads[i], NULL, calculate, &params[i]);
        if(err != 0){
            printf("Couldn't create thread!\n");
            break;
        }
        init_count++;
    }
    return init_count;
}

// Утилита для применения pthread_join() для всех созданных и работающих потоков плюс получение вычисленного ими значения числа пи
int threads_join(pthread_t *threads, long thread_count, double *pi){
    if(thread_count < 1 || pi == NULL || threads == NULL){
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

    long thread_count;
    char *tc_string = argv[1];
    errno = 0;
    char *end = "";
    thread_count = strtol(tc_string, &end, 10); // конвертируем argv значение в чиселку
    if(errno != 0){
        perror("Can't convert given number!\n");
        return EXIT_FAILURE;
    }
    if(thread_count < 1){
        printf("You can't create 0 or less threads!\n");
        return EXIT_FAILURE;
    }

    pthread_t threads[thread_count];
    thread_params params[thread_count];

    // Создание и инициализация потоков с параметрами
    long init_num = create_threads(threads, params, thread_count);
    if(init_num != thread_count){
        printf("Somehow we couldn't create as many threads as you wished!\n");
        return EXIT_FAILURE;
    }

    // Основное действо
    double result = 0.0;
    if(0 != threads_join(threads, init_num, &result)){
        printf("Couldn't calculate PI!\n");
        return EXIT_FAILURE;
    }

    printf("Result: pi = %.16f\n", result);
    return EXIT_SUCCESS;
}
