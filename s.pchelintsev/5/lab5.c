#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

const int Threads = 4;

void cleanup_aisle3(void* data) {
	printf("Cleanup sez: %s\n", (char*)data);
}

void* threadFn(void* data) {
	pthread_cleanup_push(cleanup_aisle3, data);

	while (1) {
		int times = 3 + rand() % 4;
		for (int i = 0; i < times; i++) {
			printf("принт%s", i == (times - 1) ? "\n" : ", ");
		}
	}

	// Realistically, this won't ever run, but it bitches and complains if you don't put it there
	// due to some macro voodoo by the pthread devs
	pthread_cleanup_pop(0);
	return NULL;
}


int main() {
	srand(time(NULL));

	pthread_t thread_id;

	char idk[] = "funny string :):):))";
	char* data = malloc(sizeof(idk));
	memcpy(data, idk, sizeof(idk));

	printf("-- Main: started thread... --\n");

	int ok = pthread_create(&thread_id, NULL, threadFn, data);
	if (ok != 0) {
		perror("Failed to create a thread. ");
		return -1;
	}

	sleep(2);
	if (pthread_cancel(thread_id) != 0) {
		perror("pthread_cancel failed (HOW\nBOTTOM TEXT): ");
	}

	printf("\n -- Main: cancelling... -- \n");

	if (pthread_join(thread_id, NULL) != 0) {
		perror("WHAT\nBOTTOM TEXT");
	}

	printf("\n // eof // \n");

	free(data);

	/*
		Q1: how do cleanups work?
		 1.1: how do handlers execute

		Q2: cancel as a syscall

		R1: add pthread_exit to the end lol
		R2: -Wall -Wpedantic
		R3: strace -f ./a.out
	*/

	pthread_exit(NULL);
}
