// i hate the concept of headers... it's 2022 for gods sake
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <semaphore.h>

#define WQ_QUEUE_CAP 10
#define WQ_STRING_CAP 80

typedef struct wqueue_t {
	char* strungs;

	sem_t semFull; // how many elements in the queue? init = 0
	sem_t semFree; // how many more elements we can fit? init = WQ_QUEUE_CAP

	// the task doesn't allow using mutexes, but i kinda need one...
	sem_t mutex; // binary semaphore used as a mutex

	size_t head; // write cursor
	size_t tail; // read cursor

	bool valid; // whether this queue is still valid; if it's dropped via wqInvalidate, it can't be used anymore
	int waiting; // amount of threads blocked on the semaphores; necessary for wqInvalidate
} WorkQueue;

void 	wqInit	(WorkQueue*);
void 	wqInvalidate	(WorkQueue*);
void 	wqFree	(WorkQueue*);
int 	wqPut	(WorkQueue*, char* msg);
int 	wqPop	(WorkQueue*, char* buf, size_t bufsize);

#ifdef PROPER_NAMES // power play
#define mymsginit(q) 		wqInit(q)
#define mymsqdrop(q) 		wqInvalidate(q)
#define mymsgdestroy(q)		wqFree(q)
#define mymsgput(q, a) 		wqPut(q, a)
#define mymsgget(q, a, b) 	wqPop(q, a, b)
#endif