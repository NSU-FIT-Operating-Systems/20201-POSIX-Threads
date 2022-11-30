#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <pthread.h>

#include "lab25.h"

void wqInit(WorkQueue* q) {
	assert(sem_init(&q->sem, 0, 0) == 0);
	q->tail = 0;

	pthread_mutex_init(&q->mut, NULL);

	size_t stringSize = sizeof(char) * WQ_STRING_CAP;

	char* chunk = (char*)malloc(WQ_QUEUE_CAP * stringSize);

	q->strungs = chunk;
}

void wqFree(WorkQueue* q) {
	free(q->strungs);
	sem_destroy(&q->sem);
	pthread_mutex_destroy(&q->mut);
}

char* wqGetString(WorkQueue* q, size_t i) {
	return q->strungs + sizeof(char) * WQ_STRING_CAP * i;
}

/* Head = Write Cursor */
size_t wqGetHead(WorkQueue* wq) {
	int head;
	sem_getvalue(&wq->sem, &head);
	if (head < 0) head = 0;

	return (wq->tail + head) % WQ_QUEUE_CAP;
}

/* Tail = Read Cursor */
size_t wqGetTail(WorkQueue* wq) {
	return wq->tail;
}

void wqAdvanceTail(WorkQueue* wq) {
	wq->tail = (wq->tail + 1) % WQ_QUEUE_CAP;
}

int wqPop(WorkQueue* wq, char* buf, size_t bufsize) {
	int ret = 0;
	while ((ret = sem_wait(&wq->sem)) != 0) {
		if (errno == EINTR) { continue; } // EINTR = interrupted by signal... we need to wait again
		perror("Error while decrementing semaphore: ");
		exit(0); // EINVAL is critical; it's best not to proceed
		break;
	}

	// don't copy more than they can handle
	size_t cpySize = bufsize < WQ_STRING_CAP ? bufsize : WQ_STRING_CAP;
	memcpy(buf, wqGetString(wq, wqGetTail(wq)), cpySize);

	pthread_mutex_lock(&wq->mut);
	wqAdvanceTail(wq);
	pthread_mutex_unlock(&wq->mut);

	return cpySize;
}

int wqPut(WorkQueue* wq, char* buf) {
	pthread_mutex_lock(&wq->mut);

	char* str = wqGetString(wq, wqGetHead(wq));
	assert(sem_post(&wq->sem) == 0);

	int len = WQ_STRING_CAP;

	for (int i = 0; i < WQ_STRING_CAP; i++) {
		str[i] = buf[i];

		if (buf[i] == 0) {
			len = i;
			break;
		}
	}

	pthread_mutex_unlock(&wq->mut);

	return len;
}
