#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#define TIMES 10

pthread_mutex_t mtx;
pthread_cond_t cv;
bool syncBool = true; // true = parent's turn to print; false = child's
// parent should be printing first

void waitBool(bool tgt) {
	pthread_mutex_lock(&mtx);
	while (syncBool != tgt) // spurrious wakeups
		pthread_cond_wait(&cv, &mtx);
	pthread_mutex_unlock(&mtx);
}

void signalBool(bool to) {
	pthread_mutex_lock(&mtx);
	syncBool = to;
	pthread_cond_broadcast(&cv);
	pthread_mutex_unlock(&mtx);
}

void child_op() {
	printf("Child print\n");
}

void parent_op() {
	printf("Parent print\n");
}

void* child(void* blah) {
	for (int i = 0; i < TIMES; i++) {
		waitBool(false);

		child_op();
		signalBool(true);
	}

	return NULL;
}

int main(int argc, char** argv) {
	pthread_t thread_id;

	pthread_cond_init(&cv, NULL);
	pthread_mutex_init(&mtx, NULL);

	int ok = pthread_create(&thread_id, NULL, &child, NULL);
	if (ok != 0) {
		perror("Failed to create a thread. ");
		return -1;
	}

	for (int i = 0; i < TIMES; i++) {
		parent_op();
		signalBool(false);

		waitBool(true);
	}
}