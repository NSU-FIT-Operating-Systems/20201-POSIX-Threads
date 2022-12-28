#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>

#include <pthread.h>

#include "lab25.h"

/* Ctor/dtor */
void wqInit(WorkQueue* q) {
	assert(sem_init(&q->semFull, 0, 0) == 0);
	assert(sem_init(&q->semFree, 0, WQ_QUEUE_CAP) == 0);
	assert(sem_init(&q->mutex, 0, 1) == 0);
	q->head = 0;
	q->tail = 0;
	q->valid = 1;
	q->waiting = 0;

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

// Inline because gcc doesn't warn about unused inline functions; not sure if intended
inline static void debugprint(const char *fmt, ...) {
#ifdef DEBUG
	va_list va;
	va_start(va, fmt);
	vprintf(fmt, va);
	va_end(va);
#endif
}

static void _wqLock(WorkQueue* q) {
	assert(sem_wait(&q->mutex) == 0);
}

static void _wqUnlock(WorkQueue* q) {
	assert(sem_post(&q->mutex) == 0);
}

// eucledian modulo
// (not the same as the modulo operator with negative numbers)
static size_t _wqWrap(long long n, long long add) {
	size_t mod = WQ_QUEUE_CAP;
	long long x = (n + add) % mod;

	return x < 0 ? x + mod : x;
}

// Decrement one semaphore, then lock the mutex, then increment the other
// Useful in push/pop operations
// Returns false if the queue is no longer valid
static bool _wqSemDecrLockIncr(WorkQueue* q, sem_t* decr, sem_t* incr) {

	/*
		If we don't lock here, with wqInvalidate running, we may be stuck in a state where
		`q->waiting` didn't increment when wqInvalidate read it, and
		`q->valid` didn't get set when we read it,
		so we'd get blocked on the semaphore forever
	*/

	_wqLock(q);
	if (!q->valid) {
		_wqUnlock(q);
		return false;
	}
	q->waiting++;
	_wqUnlock(q);

	int ret = 0;

	while ((ret = sem_wait(decr)) != 0) {
		if (errno == EINTR) { continue; } // EINTR = interrupted by signal... we need to wait again
		perror("Error while decrementing semaphore: ");
		exit(0); // EINVAL is critical; it's best not to proceed
		break;
	}

	// don't lock anything if the queue is no longer valid
	if (!q->valid)
		return false;

	_wqLock(q);
	q->waiting--;
	sem_post(incr);

	return true;
}

static char* _wqGetString(WorkQueue* q, size_t i) {
	return q->strungs + sizeof(char) * WQ_STRING_CAP * i;
}



/* Head (= write cursor) */

static size_t wqGetHead(WorkQueue* q) {
	return q->head;
}

static void wqAdvanceHead(WorkQueue* q) {
	q->head = _wqWrap(q->head, 1);
}


/* Tail (= read cursor) */
static size_t wqGetTail(WorkQueue* q) {
	return q->tail;
}

static void wqAdvanceTail(WorkQueue* q) {
	q->tail = _wqWrap(q->tail, 1);
}



/* Push/pop operations */
int wqPop(WorkQueue* q, char* buf, size_t bufsize) {
	if (!_wqSemDecrLockIncr(q, &q->semFull, &q->semFree)) {
		return 0;
	}

	// don't copy more than they can handle
	size_t tail = wqGetTail(q);
	size_t cpySize = bufsize < WQ_STRING_CAP ? bufsize : WQ_STRING_CAP;
	memcpy(buf, _wqGetString(q, tail), cpySize);

	wqAdvanceTail(q);

	_wqUnlock(q);

	return cpySize;
}

int wqPut(WorkQueue* q, char* buf) {
	if (!_wqSemDecrLockIncr(q, &q->semFree, &q->semFull)) {
		return 0;
	}

	// Because we just advanced the head semaphore, we need to subtract 1
	// to get to the index we just claimed as "taken"
	size_t write = wqGetHead(q);
	// debugprint("Write %s at %d\n", buf, write);
	char* str = _wqGetString(q, write);
	wqAdvanceHead(q);

	size_t len = WQ_STRING_CAP;

	for (size_t i = 0; i < WQ_STRING_CAP; i++) {
		str[i] = buf[i];

		if (buf[i] == 0) {
			len = i;
			break;
		}
	}

	_wqUnlock(q);

	return len;
}



/* Invalidating the queue */

// See _wqSemDecrLockIncr for reasoning on the lock
void wqInvalidate(WorkQueue* q) {
	if (q->waiting == 0 && !q->valid) { // optimization
		return;
	}

	_wqLock(q);
	q->valid = false;
	debugprint("wqInvalidate: %d threads awaiting unlock\n", q->waiting);
	// Unblock every waiting semaphore
	// This could be improved by tracking how many threads
	// are waiting on every individual semaphore, instead of both
	for (int i = 0; i < q->waiting; i++) {
		sem_post(&q->semFull);
		sem_post(&q->semFree);
	}

	q->waiting = 0;
	_wqUnlock(q);
}
