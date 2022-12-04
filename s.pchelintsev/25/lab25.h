// i hate the concept of headers... it's 2022 for gods sake
#pragma once

#include <semaphore.h>

#define WQ_QUEUE_CAP 10
#define WQ_STRING_CAP 80

typedef struct wqueue_t {
	char* strungs;

	sem_t semFull; // how many elements in the queue? init = 0
	sem_t semFree; // how many more elements we can fit? init = WQ_QUEUE_CAP

	// the task doesn't allow using mutexes, but i kinda need one...
	sem_t mutex; // binary semaphore used as a mutex

	int tail; // read cursor
} WorkQueue;

void 	wqInit	(WorkQueue*);
void 	wqClear	(WorkQueue*);
void 	wqFree	(WorkQueue*);
int 	wqPut	(WorkQueue*, char* msg);
int 	wqPop	(WorkQueue*, char* buf, size_t bufsize);

#ifdef PROPER_NAMES // power play
#define mymsginit(q) 		wqInit(q)
#define mymsqdrop(q) 		wqClear(q)
#define mymsgdestroy(q)		wqFree(q)
#define mymsgput(q, a) 		wqPut(q, a)
#define mymsgget(q, a, b) 	wqPop(q, a, b)
#endif