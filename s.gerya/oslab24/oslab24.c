#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <semaphore.h>

#define WIDGETSUFFICE (60)

#define PRODUCTIONTIMEA (1000000) /*ms*/
#define PRODUCTIONTIMEB (2000000) /*ms*/
#define PRODUCTIONTIMEC (3000000) /*ms*/

/*used by A, B, C, module and widget respectively */
int GDetailATotal = 0;
int GDetailBTotal = 0;
int GDetailCTotal = 0;
int ModuleTotal = 0;
int WidgetTotal = 0;

sem_t semA;
sem_t semB;
sem_t semModuleAB;
sem_t semC;

void produceDetailA() {
    while(1) {
        usleep(PRODUCTIONTIMEA);
        /*spurious wakeup -> miraculous productivity*/
        sem_post(&semA); /*overflow -> still good*/
        GDetailATotal += 1;
        if (WidgetTotal >= WIDGETSUFFICE) {
            return;
        }
    }
}

void produceDetailB() {
    while(1) {
        usleep(PRODUCTIONTIMEB);
        /*spurious wakeup -> miraculous productivity*/
        sem_post(&semB); /*overflow -> still good*/
        GDetailBTotal += 1;
        if (WidgetTotal >= WIDGETSUFFICE) {
            return;
        }
    }
}

void produceDetailC() {
    while(1) {
        usleep(PRODUCTIONTIMEC);
        /*spurious wakeup -> miraculous productivity*/
        sem_post(&semC); /*overflow -> still good*/
        GDetailCTotal += 1;
        if (WidgetTotal >= WIDGETSUFFICE) {
            return;
        }
    }
}

void createModuleWAB() {
    /*creation*/
    ModuleTotal += 1;
}

void produceModule() {
    int semAWaitErr = 0;
    do {
        semAWaitErr = sem_wait(&semA);
        /*error -> -1 and errno*/
    } while((semAWaitErr == -1) && (errno == EINTR));
    /*presuming given semaphore is valid*/

    int semBWaitErr = 0;
    do {
        semBWaitErr = sem_wait(&semB);
        /*error -> -1 and errno*/
    } while((semBWaitErr == -1) && (errno == EINTR));
    /*presuming given semaphore is valid*/

    createModuleWAB();
    sem_post(&semModuleAB); /*overflow -> still good*/
}

void produceModuleCont() {
    while(1) {
        if (WidgetTotal >= WIDGETSUFFICE) {
            return;
        } else {
            produceModule();
        }
    }
}

void createWidgetWCModule() {
    /*creation*/
    WidgetTotal += 1;
}

void produceWidget() {
    int semCWaitErr = 0;
    do {
        semCWaitErr = sem_wait(&semC);
        /*error -> -1 and errno*/
    } while((semCWaitErr == -1) && (errno == EINTR));
    /*presuming given semaphore is valid*/

    int semModuleABWaitErr = 0;
    do {
        semModuleABWaitErr = sem_wait(&semModuleAB);
        /*error -> -1 and errno*/
    } while((semModuleABWaitErr == -1) && (errno == EINTR));
    /*presuming given semaphore is valid*/

    createWidgetWCModule();
}

void produceWidgetCont() {
    while(1) {
        if (WidgetTotal >= WIDGETSUFFICE) {
            return;
        } else {
            produceWidget();
        }
    }
}

void printResults() {
    fprintf(stdout, "TOTAL:\n"
                    "---A DETAILS : [%d]\n"
                    "---B DETAILS : [%d]\n"
                    "---C DETAILS : [%d]\n"
                    "---AB MODULES: [%d]\n"
                    "---WIDGETS   : [%d]\n",
            GDetailATotal, GDetailBTotal, GDetailCTotal, ModuleTotal, WidgetTotal);
}

void destroySemaphores() {
    if (sem_destroy(&semA) == -1) {
        fprintf(stderr, "Could not destroy semaphore for A\n");
    }
    if (sem_destroy(&semB) == -1) {
        fprintf(stderr, "Could not destroy semaphore for B\n");
    }
    if (sem_destroy(&semModuleAB) == -1) {
        fprintf(stderr, "Could not destroy semaphore for ABModule\n");
    }
    if (sem_destroy(&semC) == -1) {
        fprintf(stderr, "Could not destroy semaphore for C\n");
    }
}

int initSemaphores() {
    if (sem_init(&semA, 0, 0) == -1) {
        fprintf(stderr, "Could not initialize semaphore for A\n");
        return -1;
    }

    if (sem_init(&semB, 0, 0) == -1) {
        fprintf(stderr, "Could not initialize semaphore for B\n");
        if (sem_destroy(&semA) == -1) {
            fprintf(stderr, "Could not destroy semaphore for A\n");
        }
        return -1;
    }

    if (sem_init(&semModuleAB, 0, 0) == -1) {
        fprintf(stderr, "Could not initialize semaphore for ABModule\n");
        if (sem_destroy(&semA) == -1) {
            fprintf(stderr, "Could not destroy semaphore for A\n");
        }
        if (sem_destroy(&semB) == -1) {
            fprintf(stderr, "Could not destroy semaphore for B\n");
        }
        return -1;
    }

    if (sem_init(&semC, 0, 0) == -1) {
        fprintf(stderr, "Could not initialize semaphore for C\n");
        if (sem_destroy(&semA) == -1) {
            fprintf(stderr, "Could not destroy semaphore for A\n");
        }
        if (sem_destroy(&semB) == -1) {
            fprintf(stderr, "Could not destroy semaphore for B\n");
        }
        if (sem_destroy(&semModuleAB) == -1) {
            fprintf(stderr, "Could not destroy semaphore for ABModule\n");
        }
        return -1;
    }

    return 0;
}

int main() {

    /*initialize semaphores*/
    if (-1 == initSemaphores()) {
        return -1;
    }
    /*---------------------*/

    int numThreads = 5;
    pthread_t threads[numThreads];
    int incomp_flag = 0;

    int error = pthread_create(&threads[0], NULL, (void *(*)(void *)) produceDetailA, NULL);
    if (0 != error) {
        fprintf(stderr, "Error when creating: [%s]\n", strerror(error));
        fprintf(stderr, "Production incomplete, failed to create prodLine A\n");
        incomp_flag = 1;
    }
    /*check if broken*/
    if (incomp_flag == 1) {
        fprintf(stdout, "Production could not be complete, shutting down\n");
        destroySemaphores();
        return -1;
    }

    error = pthread_create(&threads[1], NULL, (void *(*)(void *)) produceDetailB, NULL);
    if (0 != error) {
        fprintf(stderr, "Error when creating: [%s]\n", strerror(error));
        fprintf(stderr, "Production incomplete, failed to create prodLine B\n");
        incomp_flag = 1;
    }
    /*check if broken*/
    if (incomp_flag == 1) {
        fprintf(stdout, "Production could not be complete, shutting down\n");
        /*try to join prodLineA*/
        error = pthread_join(threads[0], NULL);
        if (0 != error) {
            fprintf(stderr, "Error when joining: [%s]\n", strerror(error));
        }
        destroySemaphores();
        return -1;
    }

    error = pthread_create(&threads[2], NULL, (void *(*)(void *)) produceModuleCont, NULL);
    if (0 != error) {
        fprintf(stderr, "Error when creating: [%s]\n", strerror(error));
        fprintf(stderr, "Production incomplete, failed to create prodLine for ABModules\n");
        incomp_flag = 1;
    }
    /*check if broken*/
    if (incomp_flag == 1) {
        fprintf(stdout, "Production could not be complete, shutting down\n");
        /*try to join prodLineA*/
        error = pthread_join(threads[0], NULL);
        if (0 != error) {
            fprintf(stderr, "Error when joining: [%s]\n", strerror(error));
        }
        /*try to join prodLineB*/
        error = pthread_join(threads[1], NULL);
        if (0 != error) {
            fprintf(stderr, "Error when joining: [%s]\n", strerror(error));
        }
        destroySemaphores();
        return -1;
    }

    error = pthread_create(&threads[3], NULL, (void *(*)(void *)) produceDetailC, NULL);
    if (0 != error) {
        fprintf(stderr, "Error when creating: [%s]\n", strerror(error));
        fprintf(stderr, "Production incomplete, failed to create prodLine C\n");
        incomp_flag = 1;
    }
    /*check if broken*/
    if (incomp_flag == 1) {
        fprintf(stdout, "Production could not be complete, shutting down\n");
        /*try to join prodLineA*/
        error = pthread_join(threads[0], NULL);
        if (0 != error) {
            fprintf(stderr, "Error when joining: [%s]\n", strerror(error));
        }
        /*try to join prodLineB*/
        error = pthread_join(threads[1], NULL);
        if (0 != error) {
            fprintf(stderr, "Error when joining: [%s]\n", strerror(error));
        }
        /*try to join prodLine for ABModules*/
        error = pthread_join(threads[2], NULL);
        if (0 != error) {
            fprintf(stderr, "Error when joining: [%s]\n", strerror(error));
        }
        destroySemaphores();
        return -1;
    }

    error = pthread_create(&threads[4], NULL, (void *(*)(void *)) produceWidgetCont, NULL);
    if (0 != error) {
        fprintf(stderr, "Error when creating: [%s]\n", strerror(error));
        fprintf(stderr, "Production incomplete, failed to create prodLine for Widgets\n");
        incomp_flag = 1;
    }
    /*check if broken*/
    if (incomp_flag == 1) {
        fprintf(stdout, "Production could not be complete, shutting down\n");
        /*try to join prodLineA*/
        error = pthread_join(threads[0], NULL);
        if (0 != error) {
            fprintf(stderr, "Error when joining: [%s]\n", strerror(error));
        }
        /*try to join prodLineB*/
        error = pthread_join(threads[1], NULL);
        if (0 != error) {
            fprintf(stderr, "Error when joining: [%s]\n", strerror(error));
        }
        /*try to join prodLine for ABModules*/
        error = pthread_join(threads[2], NULL);
        if (0 != error) {
            fprintf(stderr, "Error when joining: [%s]\n", strerror(error));
        }
        /*try to join prodLine C*/
        error = pthread_join(threads[3], NULL);
        if (0 != error) {
            fprintf(stderr, "Error when joining: [%s]\n", strerror(error));
        }
        destroySemaphores();
        return -1;
    }

    /*assuming all thread were created and are running*/
    for (int i = 0; i < numThreads; i++) {
        error = pthread_join(threads[i], NULL);
        if (0 != error) {
            fprintf(stderr, "Error when joining: [%s]\n", strerror(error));
            fprintf(stderr, "Thread [%d] failed to join\n", i);
        }
    }
    destroySemaphores();

    printResults();

    return 0;
}
