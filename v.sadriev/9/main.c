#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

#define STEPS_PER_THREAD 100000

typedef struct {
	ulong *start_idx;
	double *sum;
	pthread_mutex_t *iter_lock;
} partial_sum_t;

typedef struct {
	pthread_t thread;
	partial_sum_t *part;
	bool need_join;
} worker_t;

bool sig_to_quit = false;

void free_part(partial_sum_t *part) {
	if (part != NULL) {
		if (part->sum != NULL) {
			free(part->sum);
		}
		free(part);
	}
}

void free_worker(worker_t *worker) {
	if (worker != NULL) {
		free_part(worker->part);
		free(worker);
	}
}

partial_sum_t *allocatePart() {
	partial_sum_t *part = (partial_sum_t*) malloc(sizeof(*part));
	if (part == NULL) {
		fprintf(stderr, "Couldn't allocate part\n");
		return NULL;
	}
	part->sum = (double*) malloc(sizeof(double));
	if (part->sum == NULL) {
		fprintf(stderr, "Couldn't allocate sum in part\n");
		free(part);
		return NULL;
	}
	return part;
}

bool generate_new_start_idx(partial_sum_t *part, ulong *thread_start_idx) {
	if (pthread_mutex_lock(part->iter_lock) != 0) {
		return false;
	}
	*thread_start_idx = *part->start_idx;
	*part->start_idx += STEPS_PER_THREAD;
	if (pthread_mutex_unlock(part->iter_lock) != 0) {
		return false;
	}
	return true;
}

void *count_pi(void *arg) {
	partial_sum_t *part = (partial_sum_t*) arg;
	ulong i = 0;
	ulong until;
again:
	if (!generate_new_start_idx(part, &i)) {
		fprintf(stderr, "Lock / unlock error\n");
		pthread_exit(part);
	}
	until = i + STEPS_PER_THREAD;
	while (i < until) {
		*(part->sum) += 1.0 / ((double)i * 4.0 + 1.0);
		*(part->sum) -= 1.0 / ((double)i * 4.0 + 3.0);
		++i;
	}
	if (sig_to_quit) {
		pthread_exit(part);
	} else {
		goto again;
	}
}

void handle_sigint() {
	printf("\nGot signal\n");
	sig_to_quit = true;
}


int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "Please, pass num of threads\n");
		return 1;
	}
	char *endptr;
	ulong num_threads = strtoul(argv[1], &endptr, 10);
	if (errno != 0) {
		perror("Strtol error");
		return 2;
	}
	if (endptr == argv[1]) {
		fprintf(stderr, "No digits were found\n");
		return 3;
	}

	worker_t *workers[num_threads];
	double pi = 0;
	ulong start_idx = 0;
	struct sigaction sa;
	sa.sa_handler = &handle_sigint;
	if (sigaction(SIGINT, &sa, NULL) != 0) {
		perror("Sigaction trouble");
		return 4;
	}

	pthread_mutex_t iter_lock;
	if (pthread_mutex_init(&iter_lock, NULL) != 0) {
		perror("Couldn't init mutex");
		return 5;
	}

	for (ulong i = 0; i < num_threads; ++i) {
		worker_t *worker = (worker_t*) malloc(sizeof(worker_t));
		workers[i] = worker;
		if (worker == NULL) {
			fprintf(stderr, "Couldn't allocate worker\n");
			continue;
		}
		worker->need_join = true;
		worker->part = allocatePart();
		if (worker->part == NULL) {
			fprintf(stderr, "Couldn't allocate part sum of %lu thread\n", i);
			worker->need_join = false;
			continue;
		}
		worker->part->iter_lock = &iter_lock;
		worker->part->start_idx = &start_idx;
		if (pthread_create(&worker->thread, NULL, &count_pi, worker->part) != 0) {
			perror("Couldn't create thread");
			worker->need_join = false;
		}
	}

	void *ret = NULL;
	for (ulong i = 0; i < num_threads; ++i) {
		if (workers[i] != NULL) {
			if (workers[i]->need_join) {
				if (pthread_join(workers[i]->thread, &ret) != 0) {
					perror("Couldn't join threads");
					free_worker(workers[i]);
					continue;
				}
				partial_sum_t *part = (partial_sum_t*) ret;
				pi += *part->sum;
			}
			free_worker(workers[i]);
		}
	}
	pthread_mutex_destroy(&iter_lock);

	pi = pi * 4.0;
	double standard_pi = 3.141592653589793;
	printf("Pi done:     %.15g \n", pi);
	printf("Standard Pi: %.15g\n", standard_pi);
	printf("Accuracy:    %.15g\n", standard_pi - pi);
	return 0;
}
