#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>

#define TIMES 10

pthread_mutex_t loop_mtx;
pthread_mutex_t childlimit_mtx;
pthread_mutex_t parentlimit_mtx;
pthread_mutex_t sync_mtx;

#define MUTEXES 3
pthread_mutex_t mtxes[MUTEXES];

static void lock(int n) {
	assert(n < MUTEXES);
	pthread_mutex_lock(&mtxes[n]);
}

static void unlock(int n) {
	assert(n < MUTEXES);
	pthread_mutex_unlock(&mtxes[n]);
}

static pthread_mutex_t* getMtx(int n) {
	return &mtxes[n];
}

bool child_ran = false;

void child_op() {
	printf("Child print\n");
}

void parent_op() {
	printf("Parent print\n");
}

void* child(void* blah) {
	lock(2);
	child_ran = true;

	/* 	Parent: 1
		Child: 2 */

	for (int i = 0; i < TIMES; i++) {
		lock(1); // wait until parent prints...

		/* 	Parent: 0
			Child: 1 2 */

		child_op();

		// unblock parent
		unlock(2);

		// wait until parent gets unblocked
		lock(0);

		/* 	Parent: 2
			Child: 0 1 */

		// restore status quo
		unlock(1);
		lock(2);

		/* 	Parent: 1
			Child: 0 2 */

		unlock(0);
	}

	unlock(2);

	return NULL;
}

int main(int argc, char** argv) {
	pthread_t thread_id;

	for (int i = 0; i < MUTEXES; i++) {
		pthread_mutex_init(&mtxes[i], NULL);
	}

	lock(1);

	int ok = pthread_create(&thread_id, NULL, &child, NULL);
	if (ok != 0) {
		perror("Failed to create a thread. ");
		return -1;
	}

	// lame but whatever
	while (!child_ran) {
		usleep(50);
	}

	/* 	Parent: 1
		Child: 2 */
	for (int i = 0; i < TIMES; i++) {
		parent_op();

		// 0 waits until child prints
		// 1 unlocks child
		lock(0); 	// 	Parent: 0
		unlock(1);	//	Child: 2


		// child holds 2; wait until it prints...
		lock(2);
		unlock(0); // allow child to restore status quo

		/* 	Parent: 2
			Child: 1 */

		// restore status quo
		lock(1);
		unlock(2);
	}

	unlock(1);
	pthread_join(thread_id, NULL);

	pthread_mutex_destroy(getMtx(2));
	pthread_mutex_destroy(getMtx(1));
	pthread_mutex_destroy(getMtx(0));


	pthread_exit(NULL);
}