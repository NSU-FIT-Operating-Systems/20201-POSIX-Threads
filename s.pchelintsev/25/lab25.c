#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <pthread.h>

#include "lab25.h"

/* Ctor/dtor */
void wqInit(WorkQueue* q) {
	assert(sem_init(&q->semFull, 0, 0) == 0);
	assert(sem_init(&q->semFree, 0, WQ_QUEUE_CAP) == 0);
	assert(sem_init(&q->mutex, 0, 1) == 0);
	q->tail = 0;

	size_t stringSize = sizeof(char) * WQ_STRING_CAP;

	char* chunk = (char*)malloc(WQ_QUEUE_CAP * stringSize);

	q->strungs = chunk;
}

void wqFree(WorkQueue* q) {
	free(q->strungs);
	sem_destroy(&q->semFull);
	sem_destroy(&q->semFree);
	sem_destroy(&q->mutex);
}



/* Internal */

void _wqLock(WorkQueue* q) {
	sem_wait(&q->mutex);
}

void _wqUnlock(WorkQueue* q) {
	sem_post(&q->mutex);
}

// eucledian modulo
// (not the same as the modulo operator with negative numbers)
int _wqWrap(int n, int add) {
	int mod = WQ_QUEUE_CAP;
	int x = (n + add) % mod;

	return x < 0 ? x + mod : x;
}

// Decrement one semaphore, then lock the mutex, then increment the other
// Useful in push/pop operations
void _wqSemDecrLockIncr(WorkQueue* q, sem_t* decr, sem_t* incr) {
	int ret = 0;

	while ((ret = sem_wait(decr)) != 0) {
		if (errno == EINTR) { continue; } // EINTR = interrupted by signal... we need to wait again
		perror("Error while decrementing semaphore: ");
		exit(0); // EINVAL is critical; it's best not to proceed
		break;
	}

	_wqLock(q);
	sem_post(incr);
}

char* _wqGetString(WorkQueue* q, size_t i) {
	return q->strungs + sizeof(char) * WQ_STRING_CAP * i;
}



/* Head (= write cursor) */

size_t wqGetHead(WorkQueue* q) {
	int head;
	sem_getvalue(&q->semFull, &head);
	if (head < 0) head = 0;

	return _wqWrap(q->tail, head);
}



/* Tail (= read cursor) */
size_t wqGetTail(WorkQueue* q) {
	return q->tail;
}

void wqAdvanceTail(WorkQueue* q) {
	q->tail = _wqWrap(q->tail, 1);
}



/* Push/pop operations */
int wqPop(WorkQueue* q, char* buf, size_t bufsize) {
	_wqSemDecrLockIncr(q, &q->semFull, &q->semFree);

	// don't copy more than they can handle
	size_t cpySize = bufsize < WQ_STRING_CAP ? bufsize : WQ_STRING_CAP;
	memcpy(buf, _wqGetString(q, wqGetTail(q)), cpySize);

	wqAdvanceTail(q);

	_wqUnlock(q);

	return cpySize;
}

int wqPut(WorkQueue* q, char* buf) {
	_wqSemDecrLockIncr(q, &q->semFree, &q->semFull);

	// Because we just advanced the head semaphore, we need to subtract 1
	// to get to the index we just claimed as "taken"
	int write = _wqWrap(wqGetHead(q), -1);
	char* str = _wqGetString(q, write);

	int len = WQ_STRING_CAP;

	for (int i = 0; i < WQ_STRING_CAP; i++) {
		str[i] = buf[i];

		if (buf[i] == 0) {
			len = i;
			break;
		}
	}

	_wqUnlock(q);

	return len;
}
