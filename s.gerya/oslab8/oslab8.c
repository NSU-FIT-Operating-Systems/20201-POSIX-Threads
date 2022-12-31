#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#define _USE_MATH_DEFINES
#include <math.h>

#define N_O_ITERATIONS 250000
//for each thread, so (NOTHREADS * NOITERATIONS) total summands

struct ARGS {
    int numMe;
    int numTotal;
};

double* calcPart(struct ARGS* args) {

    //----------------------------------------------------
    double* myPart = (double*)malloc(sizeof(double));
    if (NULL == myPart) {
        perror("malloc: ");
        pthread_exit(NULL);
        return NULL;
    }
    //----------------------------------------------------
    *myPart = 0.0;

    for (int i = 0; i < N_O_ITERATIONS ; i++) {

        int divisor = (2 * ((i * args->numTotal) + args->numMe)) + 1;
        int sign = ( ( (((i * args->numTotal) + args->numMe) % 2) == 0 ) ? (1) : (-1) );

        *myPart += ((1.0 / (double)divisor) * (double)sign);
    }

    printf("Part {%d} done, my part: [%.15f]\n", args->numMe, *myPart);

    pthread_exit(myPart);
    return NULL;
}

int main(int argc, char** argv) {

    if (1 == argc) {
        fprintf(stderr, "Number expected as a parameter\n");
        return -1;
    }

    char *endptr;
    errno = 0;
    long numThreads = strtol(argv[1], &endptr, 10);
    if (errno != 0) {
        perror("strtol: ");
        return -1;
    }
    if (endptr == argv[1]) {
        fprintf(stderr, "No digits found\n");
        return -1;
    }

    pthread_t threads[numThreads];
    struct ARGS args[numThreads];
    int failedThreads[numThreads];

    int incomp_flag = 0;
    for (int i = 0; i < numThreads; i++) {
        args[i].numMe = i;
        args[i].numTotal = numThreads;
        int error = pthread_create(&threads[i], NULL, (void *(*)(void *)) calcPart, &args[i]);
        if (0 != error) {
            fprintf(stderr, "Error when creating: [%s]\n", strerror(error));
            fprintf(stderr, "Calculation incomplete, failed to create thread [%d]\n", i);
            incomp_flag = 1;
            failedThreads[i] = 1;
            continue;
        }
        failedThreads[i] = 0;
    }

    double number;
    double* retval = &number;
    double total = 0.0;
    for (int i = 0; i < numThreads; i++) {
        if (1 == failedThreads[i]) {
            continue;
        }
        int error = pthread_join(threads[i], (void **) &retval);
        if (0 != error) {
            fprintf(stderr, "Error when joining: [%s]\n", strerror(error));
            //return -1;
            fprintf(stderr, "Calculation incomplete, thread [%d] failed to join\n", i);
            incomp_flag = 1;
            continue;
        }
        if (NULL == retval) {
            fprintf(stderr, "Calculation incomplete, thread [%d] failed to allocate\n", i);
            incomp_flag = 1;
        } else {
            total += *retval;
            //-------------------
            free(retval);
            //-------------------
        }
    }

    if (1 == incomp_flag) {
        printf("The resulting pi was not properly calculated, result: [%.15f]\n", 4.0 * total);
    } else {
        printf("The resulting pi is aprox. [%.15f]\n", 4.0 * total);
        const long double moreAccPi = M_PI;
        printf("(math.h library says Pi is aprox. [%.15Lf])\n", moreAccPi);
        long double delta = moreAccPi - (4.0 * total);
        printf("(delta is [%.15Lf])\n", delta);
    }

    pthread_exit(0);
    return 0;
}
