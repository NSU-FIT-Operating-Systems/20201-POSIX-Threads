#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#define N_O_LINES (10)

void WriteLines() {
    pid_t pid = getpid();
    pid_t ppid = getppid();
    //pid_t ppid = getpid();
    for (int i = 0; i < N_O_LINES; i++) {
        printf("Line number {%d}, pid is [%d], ppid is (%d)\n", i, pid, ppid);
    }
    pthread_exit(NULL);
}


int main() {
    pthread_t threadID;
    int error = pthread_create(&threadID, NULL, WriteLines, NULL);
    if (0 != error) {
        fprintf(stderr, "Error when creating: [%s]\n", strerror(error));
        return -1;
    }

    WriteLines(); //move below pthread_join and stuff for OSlab2

    int stack_int;
    int* retval = &stack_int;
    error = pthread_join(threadID, &retval); //joinable by default, NULL attr -> default
    if (0 != error) {
        fprintf(stderr, "Error when joining: [%s]\n", strerror(error));
        return -1;
    }

    return 0;
}
