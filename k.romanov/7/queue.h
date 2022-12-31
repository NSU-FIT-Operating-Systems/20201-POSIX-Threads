#ifndef COPY_QUEUE_H
#define COPY_QUEUE_H
#include <pthread.h>

typedef struct dirPair{
    char * src;
    char * dest;
}dirpair;

typedef struct dirNode{
    dirpair * dir;
    mode_t mode;
    struct dirNode * next;
}dirnode;

typedef struct dirQueue{
    dirnode * head;
    dirnode ** tail;
    pthread_mutex_t * lock;
    pthread_cond_t * cond;
}dirqueue;

int initQueue(dirqueue ** queue);

int pushQueue(dirqueue * queue, dirpair * dir, mode_t mode);

dirpair * popQueue(dirqueue * queue);

void freeQueue(dirqueue * queue);

#endif //COPY_QUEUE_H
