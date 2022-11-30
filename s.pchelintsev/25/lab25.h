// i hate the concept of headers... it's 2022 for gods sake
#pragma once

#include <semaphore.h>

#define WQ_QUEUE_CAP 10
#define WQ_STRING_CAP 80

typedef struct wqueue_t {
	char* strungs;

	sem_t sem;
	int tail;

	pthread_mutex_t mut;
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
#define mymsgput(q) 		wqPut(q)
#define mymsgget(q) 		wqPop(q)
#endif