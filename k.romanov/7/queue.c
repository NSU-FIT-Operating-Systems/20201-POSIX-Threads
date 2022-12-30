#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <sys/time.h>
#include "queue.h"

int initQueue(dirqueue ** queue){
    dirqueue * newQueue = (dirqueue*)calloc(1, sizeof(dirqueue));
    if(newQueue == NULL){
        return 1;
    }
    newQueue->lock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    if(pthread_mutex_init(newQueue->lock, NULL)){
        return 1;
    }
    newQueue->cond = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));
    if(pthread_cond_init(newQueue->cond, NULL)){
        return 1;
    }
    newQueue->tail = &(newQueue->head);
    *queue = newQueue;
    return 0;
}

int pushQueue(dirqueue * queue, dirpair * dir, mode_t mode){
    pthread_mutex_lock(queue->lock);
    dirnode * el = (dirnode*)malloc(sizeof(dirnode));
    if(el == NULL){
        pthread_mutex_unlock(queue->lock);
        printf("Could not initialize queue node\n");
        return 1;
    }
    el->dir = dir;
    el->mode = mode;
    el->next = NULL;
    *(queue->tail) = el;
    queue->tail = &(el->next);
    pthread_mutex_unlock(queue->lock);
    pthread_cond_broadcast(queue->cond);
    return 0;
}

dirpair * popQueue(dirqueue * queue){
    struct timeval now;
    gettimeofday(&now, NULL);
    struct timespec ts = {now.tv_sec + 3, 0};

    pthread_mutex_lock(queue->lock);
    int retErr = pthread_cond_timedwait(queue->cond, queue->lock, &ts);
    pthread_mutex_unlock(queue->lock);
    if(retErr == 0) {
        if(queue->head == NULL){
            return NULL;
        }
        dirnode *el = queue->head;
        dirpair *ret = el->dir;
        queue->head = el->next;
        return ret;
    }
    pthread_mutex_unlock(queue->lock);
    return NULL;
}

void freeQueue(dirqueue * queue){
    pthread_mutex_destroy(queue->lock);
    free(queue->lock);
    pthread_cond_destroy(queue->cond);
    free(queue->cond);
    dirnode * el = queue->head;
    while(el != NULL) {
        dirnode *next = el->next;
        free(el->dir);
        free(el);
        el = next;
    }
}
