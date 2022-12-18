#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>

#define BLOCK_SIZE 15000000

static bool die = false;

static void sigint_handler(int sig) {
	signal(sig, SIG_IGN);
	const char str[] = "Ctrl+C received, aborting ASAP...\n";
	write(STDOUT_FILENO, str, sizeof(str));
	die = true;
}

typedef struct work_t {
	int id;
	double result;

	pthread_t worker;
} ThreadWork;

pthread_mutex_t mtx;
pthread_cond_t cv;
bool letsgo = false;

int global = 0;

void doThreadWork(void* arg) {
	ThreadWork* work = (ThreadWork*)arg;

	double ret = 0;
	uint64_t i = 0;

	while (1) {
		int block = __atomic_fetch_add(&global, 1, __ATOMIC_SEQ_CST);
		i = (uint64_t)block * BLOCK_SIZE;
		uint64_t end = i + BLOCK_SIZE;

		for (; i < end; i++) {
			ret += 1.0 / (i * 4.0 + 1.0);
			ret -= 1.0 / (i * 4.0 + 3.0);
		}

		if (die) {
			work->result = ret * 4;
			pthread_exit(NULL);
			break; // ?
		}
	}
}

void* countPartialSum(void* arg) {
	pthread_mutex_lock(&mtx);
	while (!letsgo) // spurrious wakeups
		pthread_cond_wait(&cv, &mtx);
	pthread_mutex_unlock(&mtx);

	doThreadWork(arg);

	pthread_exit(NULL);
}

void printUsage() {
	printf("Pass the thread number.\n");
	exit(-1);
}

// TODO: handle not being able to create a thread

int main(int argc, char** argv) {
	if (argc < 2)
		printUsage();

	signal(SIGINT, sigint_handler);

	int threadAmt = atoi(argv[1]);
	int real_threadAmt = threadAmt;

	if (threadAmt < 1) {
		printf("\"%s\" isn't a valid thread count.\n", argv[1]);
		printUsage();
	}

	if (argc >= 3) { // 2nd, optional arg passed: iter amt
		long long newIter = strtoll(argv[2], NULL, 10);
		if (errno != ERANGE) {
			printf("Using a custom iteration count: %lld\n", newIter);
		} else {
			printf("Couldn't recognize \"%s\" as an iteration count.", argv[2]);
			return -1;
		}
	}

	if (argc >= 4) { // 3nd, optional arg passed: REAL amt of threads
		long long realThreads = strtoll(argv[3], NULL, 10);
		if (errno != ERANGE) {
			real_threadAmt = realThreads;
			printf("Faking thread creation failure: %lld/%d\n", realThreads, threadAmt);
		} else {
			printf("Couldn't recognize \"%s\" as the real thread count.", argv[3]);
			return -1;
		}
	}

	// i don't know if VLA > malloc
	ThreadWork* works = malloc(threadAmt * sizeof(ThreadWork));

	pthread_cond_init(&cv, NULL);
	pthread_mutex_init(&mtx, NULL);

	int actual_threads = 0;
	for (int i = 0; i < threadAmt; i++) {
		if (i >= real_threadAmt) { break; }

		ThreadWork* wrk = &works[i];
		if (pthread_create(&wrk->worker, NULL, countPartialSum, &works[i]) != 0) {
			perror("failed to create new thread");
			break;
		}

		actual_threads++;
	}

	if (actual_threads == 0) {
		printf("failed to allocate any thread; exiting...\n");
		free(works);
		pthread_exit(NULL);
	}

	printf("%d threads created...\n\tAwaiting Ctrl+C.\n", actual_threads);

	for (int i = 0; i < actual_threads; i++) {
		ThreadWork* wrk = &works[i];
		wrk->id = i;
	}

	pthread_mutex_lock(&mtx);
	letsgo = true;
	pthread_cond_broadcast(&cv);
	pthread_mutex_unlock(&mtx);

	double pi = 0;
	double real_pi = M_PI;

	for (int i = 0; i < actual_threads; i++){
		if (pthread_join(works[i].worker, NULL) != 0){
			perror("failed to join thread");
			pthread_exit(NULL);
		}

		pi += works[i].result;
	}

	printf("%.11f\n", pi);
	printf("(error: %.11f)\n", real_pi - pi);
	free(works);
	pthread_exit(NULL); // E?
}