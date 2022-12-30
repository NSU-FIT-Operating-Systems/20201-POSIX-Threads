#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

#define COEFFICIENT (1)

void* WriteLine(void* args) {
    char* line = (char*)args;
    //---------------------------------
    int len = strlen(line);
    int delay = len * COEFFICIENT;
    sleep(delay);
    //---------------------------------
    printf("\"%s\"\n", line);
    return NULL;
}

int main(int argc, char** argv) {

    pthread_t threads[argc - 1];

    for (int i = 1; i < argc; i++) {
        int error = pthread_create(&threads[i - 1], NULL, WriteLine, argv[i]);
        if (0 != error) {
            fprintf(stderr, "Error when creating: [%s]\n", strerror(error));
            return -1;
        }
    }

    for (int i = 1; i < argc; i++) {
        int stack_int;
        int* retval = &stack_int;
        int error = pthread_join(threads[i - 1], &retval); //joinable by default, NULL attr -> default
        if (0 != error) {
            fprintf(stderr, "Error when joining: [%s]\n", strerror(error));
            return -1;
        }
    }

    return 0;
}
