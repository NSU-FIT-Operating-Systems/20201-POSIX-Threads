#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

const int Threads = 4;

void* threadFn(void* a) {
	printf("пароход\n");

	while (1) {
		int times = 3 + rand() % 4;
		for (int i = 0; i < times; i++) {
			printf("принтуем%s", i == (times - 1) ? "\n" : ", ");
		}
	}

	return NULL;
}

int main() {
	srand(time(NULL));

	pthread_t thread_id;

	int ok = pthread_create(&thread_id, NULL, threadFn, NULL);
	if (ok != 0) {
		perror("Failed to create a thread. ");
		return -1;
	}

	sleep(2);
	if (pthread_cancel(thread_id) != 0) {
		perror("pthread_cancel failed (HOW\nBOTTOM TEXT): ");
	}
	printf("\n -- Смерть -- \n");

	/*
		Q1: what happens inside pthread_cancel???
		Q2: what are cancellation points and **how to know if a function has a CP**
		Q3: types of cancellation points
	*/

	pthread_exit(NULL);
}