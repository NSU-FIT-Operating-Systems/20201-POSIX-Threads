#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

enum ERRORS {
	CREATE_ERROR = 1,
	JOIN_ERROR
};

#define LINES_NUM 10
#define MUTEX_NUM 3

pthread_mutex_t mutexes[MUTEX_NUM];
bool child_started = false;

void destroy_all_mutexes() {
	for (size_t i = 0; i < MUTEX_NUM; i++) {
		pthread_mutex_destroy(&mutexes[i]);
	}
}

void unlock_all_mutexes() {
	long code = 0;
	for (size_t i = 0; i < MUTEX_NUM; ++i) {
		if ((code = pthread_mutex_unlock(mutexes + i)) && (EPERM != code)) {
			fprintf(stderr, "Unlocking error: %zu, %s\n", i, strerror(code));
			break;
		}
	}
}

void* func(void* args) {
	long code = 0;
	for (size_t i = 0; i < MUTEX_NUM - 1; ++i) {
		if ((code = pthread_mutex_lock(mutexes + i))) {
			fprintf(stderr, "C : Locking error: %zu, %s\n", i, strerror(code));
			unlock_all_mutexes();
			pthread_exit((void*)code);
		}
	}
	child_started = true;
	for (size_t i = 0; i < LINES_NUM; ++i) {
		printf("Child: %zu\n", i + 1);

		if ((code = pthread_mutex_unlock(mutexes + (i % MUTEX_NUM)))) {
			fprintf(stderr, "C : Unlocking error: %zu, %s\n", i % MUTEX_NUM, strerror(code));
			break;
		}

		if ((code = pthread_mutex_lock(mutexes + ((i + 2) % MUTEX_NUM)))) {
			fprintf(stderr, "C : Locking error: %zu, %s\n", (i + 2) % MUTEX_NUM,
					strerror(code));
			break;
		}
	}
	unlock_all_mutexes();

	pthread_exit((void*)code);
}

int main() {
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
	for (size_t i = 0; i < MUTEX_NUM; i++) {
		pthread_mutex_init(&mutexes[i], &attr);
	}
	pthread_mutexattr_destroy(&attr);

	pthread_t thread;

	long code = 0;
	if ((code = pthread_mutex_lock(mutexes + (MUTEX_NUM - 1)))) {
		fprintf(stderr, "P : Locking error: %d, %s\n", (MUTEX_NUM - 1),
				strerror(code));
		destroy_all_mutexes();
		pthread_exit((void*)code);
	}

	int creation_result = pthread_create(&thread, NULL, func, NULL);
	if (0 != creation_result) {
		fprintf(stderr, "Main error: can't create thread, result = %d\n", 
				creation_result);
		destroy_all_mutexes();
		pthread_exit((void*)CREATE_ERROR);
	}

	while (!child_started) {
		sched_yield();
	}

	for (size_t i = 0; i < LINES_NUM; ++i) {
		if ((code = pthread_mutex_lock(mutexes + (i % MUTEX_NUM)))) {
			fprintf(stderr, "P : Locking error: %zu, %s\n", i % MUTEX_NUM,
					strerror(code));
			break;
		}

		printf("Parent: %zu\n", i + 1);

		if ((code = pthread_mutex_unlock(mutexes + ((i + 2) % MUTEX_NUM)))) {
			fprintf(stderr, "P : Unlocking error: %zu, %s\n", (i + 2) % MUTEX_NUM,
					strerror(code));
			break;
		}
	}
	unlock_all_mutexes();

	int join_result = pthread_join(thread, NULL);
	if (0 != join_result) {
		fprintf(stderr, "Main error: can't join thread, result = %d\n", 
				join_result);
		destroy_all_mutexes();
		pthread_exit((void*)JOIN_ERROR);
	}

	destroy_all_mutexes();

	pthread_exit((void*)code);
}
