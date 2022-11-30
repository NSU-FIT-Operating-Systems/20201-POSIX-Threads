#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

void *thread_start_routine(void *params) {
    char *string = (char*)params;
    sleep(strlen(string));
    printf("%s\n", string);
    pthread_exit(0);
}

int main(int argc, char *argv[]) {
    if(argc < 2 || argc > 101){
        printf("Too many or too few args!");
        return -1;
    }
    pthread_t threads[100];
    int err;
    for(int i = 1; i < argc; ++i){
        err = pthread_create(&threads[i - 1], NULL, thread_start_routine, (void *) argv[i]);
        if (err != 0) {
            printf("pthread_create error: ");
            return -1;
        }
    }
    pthread_exit(0);
}
