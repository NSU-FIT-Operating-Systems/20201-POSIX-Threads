#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

void* threadFn(void* a) {
	for (int i = 0; i < 10; i++) {
		printf("<| Thread printf\n");
	}

	return NULL;
}

int main() {
	pthread_t thread_id;

	int ok = pthread_create(&thread_id, NULL, threadFn, NULL);
	if (ok != 0) {
		perror("Failed to create a thread. ");
		return -1;
	}

	// 	OPT Q1: how coroutines work?
	//  OPT Q2: memory in userspace threads?

	if (pthread_join(thread_id, NULL) != 0) {
		perror("pthread_join failed (HOW\nBOTTOM TEXT): ");
	}

	for (int i = 0; i < 10; i++) {
		printf("|> Parent printf\n");
	}
}