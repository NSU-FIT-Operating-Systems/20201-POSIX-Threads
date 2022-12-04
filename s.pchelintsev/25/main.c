#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>

#include "lab25.h"

#define PRODUCERS 2
#define CONSUMERS 2
#define EXAMPLE_STRING_CAP 100

/* Welcome to #define HELL */
#define REAP_TIME 3 // Reaper thread will invalidate the worker queue after this many seconds

#define DEBUG_RUNS 5 // if ran in debug, will only push this many strings (per producer)

#ifdef DEBUG
#define TEST_UNDERFLOWS
#endif

// kinda ugly, but that's the only way i could really think of?

void debugprint(const char *fmt, ...) {
#ifdef DEBUG
	va_list va;
	va_start(va, fmt);
	vprintf(fmt, va);
	va_end(va);
#endif
}

#define min(a, b) ( ((a) < (b)) ? (a) : (b) )
#define max(a, b) ( ((a) > (b)) ? (a) : (b) )



void generateRandomString(char* where, size_t max, int i) {
	/*int times = i; // rand() % 12;
	int cursor = 0;

	for (int i = 0; i <= times; i++) {
		snprintf(where + cursor, max - cursor, "#%d%c", i, i == times ? ' ' : '|');
		cursor = strlen(where); // unoptimized, w/e
	}

	where[min(cursor, max)] = 0;*/
	snprintf(where, max, "%d", i);
}

typedef struct worker_t {
	int id;
	WorkQueue* wq;
} Worker;

void* producer(void* wrkVoid) {
#ifdef TEST_UNDERFLOWS // Test underflows
	debugprint("Producer starting; sleeping...\n");
	sleep(1);
	debugprint("Unslept; pushing strings.\n");
#endif

	Worker* work = (Worker*)wrkVoid;

	int id = work->id;
	WorkQueue* wq = work->wq;

	for (int i = 0; ; i++) {
		char str[EXAMPLE_STRING_CAP] = "BAD!";
		generateRandomString(str, EXAMPLE_STRING_CAP, i);

#ifdef DEBUG
		debugprint("\t%d write << %s\n", id, str);
#endif

		int ok = wqPut(wq, str);
		if (ok == 0) {
			debugprint("Producer %d: WorkQueue is invalid.\n", id);
			break;
		}

#ifdef DEBUG
		if (i >= DEBUG_RUNS) break;
#endif
	}

	return NULL;
}

void* consoomer(void* wrkVoid) {
	Worker* work = (Worker*)wrkVoid;

	int id = work->id;
	WorkQueue* wq = work->wq;

	debugprint("Consumer %d started; should block...\n", id);

	for (int i = 0; ; i++) {
		char str[EXAMPLE_STRING_CAP];
		int popped = wqPop(wq, str, EXAMPLE_STRING_CAP);
		if (i == 0) {
			debugprint("Consumer %d unblocked!\n", id);
		}

#ifdef DEBUG
		debugprint("\t%d read  >> %s\n", id, str);
#else
		printf("%s\n", str);
#endif

		if (popped == 0) {
			debugprint("Consumer %d: WorkQueue is invalid.\n", id);
			break;
		}
	}


	return NULL;
}

void* reaper(void* wqVoid) {
	WorkQueue* wq = (WorkQueue*)wqVoid;

	sleep(REAP_TIME);

	printf("Reaper: invalidating WorkQueue.\n");
	wqInvalidate(wq);

	return NULL;
}

bool checkJoin(pthread_t* thr) {
	if (pthread_join(*thr, NULL) != 0) {
		perror("failed to join thread");
		return false;
	}

	return true;
}

int main() {
	srand(time(NULL));

	WorkQueue wq;
	wqInit(&wq);

	pthread_t prods[PRODUCERS];
	pthread_t cons[CONSUMERS];

	Worker wrks[PRODUCERS + CONSUMERS];

	for (int i = 0; i < PRODUCERS; i++) {
		wrks[i].id = i;
		wrks[i].wq = &wq;

		if (pthread_create(&prods[i], NULL, producer, &wrks[i]) != 0) {
			perror("failed to create new producer thread");
			break;
		}
	}

	for (int i = 0; i < CONSUMERS; i++) {
		wrks[i].id = i;
		wrks[i].wq = &wq;

		if (pthread_create(&cons[i], NULL, consoomer, &wrks[i]) != 0) {
			perror("failed to create new consumer thread");
			break;
		}
	}

	pthread_t reaperThread;
	if (pthread_create(&reaperThread, NULL, reaper, &wq) != 0) {
		perror("failed to create reaper thread");
		pthread_exit(NULL);
		return 0;
	}

	for (int i = 0; i < PRODUCERS; i++) {
		if (!checkJoin(&prods[i]) || !checkJoin(&cons[i]))
			pthread_exit(NULL);
	}

	if (!checkJoin(&reaperThread))
		pthread_exit(NULL);

	wqFree(&wq);

	pthread_exit(NULL);
}