#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#include "lab25.h"

#define PRODUCERS 4
#define CONSUMERS 4
#define EXAMPLE_STRING_CAP 100

#define min(a, b) ( ((a) < (b)) ? (a) : (b) )
#define max(a, b) ( ((a) > (b)) ? (a) : (b) )

void generateRandomString(char* where, size_t max) {
	int times = rand() % 12;
	int cursor = 0;

	for (int i = 0; i <= times; i++) {
		snprintf(where + cursor, max - cursor, "String #%d%c", i, i == times ? ' ' : '|');
		cursor = strlen(where); // unoptimized, w/e
	}

	where[min(cursor, max)] = 0;
}

void* producer(void* wqVoid) {
	WorkQueue* wq = (WorkQueue*)wqVoid;

	while (1) {
		char str[EXAMPLE_STRING_CAP] = "BAD!";
		generateRandomString(str, EXAMPLE_STRING_CAP);

		wqPut(wq, str);
	}

	return NULL;
}

void* consoomer(void* wqVoid) {
	WorkQueue* wq = (WorkQueue*)wqVoid;

	while (1) {
		char str[EXAMPLE_STRING_CAP];
		wqPop(wq, str, EXAMPLE_STRING_CAP);
		printf("Consumer popped off queue: %s\n", str);
	}

	return NULL;
}

int main() {
	srand(time(NULL));

	WorkQueue wq;
	wqInit(&wq);

	pthread_t prods[PRODUCERS];
	pthread_t cons[CONSUMERS];

	for (int i = 0; i < PRODUCERS; i++) {
		if (pthread_create(&prods[i], NULL, producer, &wq) != 0) {
			perror("failed to create new producer thread");
			break;
		}
	}

	for (int i = 0; i < CONSUMERS; i++) {
		if (pthread_create(&cons[i], NULL, consoomer, &wq) != 0) {
			perror("failed to create new consumer thread");
			break;
		}
	}

	for (int i = 0; i < PRODUCERS; i++) {
		if (pthread_join(prods[i], NULL) != 0) {
			perror("failed to join producer thread");
			pthread_exit(NULL);
		}
	}

	for (int i = 0; i < CONSUMERS; i++) {
		if (pthread_join(cons[i], NULL) != 0) {
			perror("failed to join consumer thread");
			pthread_exit(NULL);
		}
	}

	wqFree(&wq);

	pthread_exit(NULL);
}