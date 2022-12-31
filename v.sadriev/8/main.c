#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>


typedef struct {
	ulong num_threads;
	ulong startIdx;
	ulong num_steps;
	double *sum;
} partial_sum_t;

typedef struct {
	pthread_t thread;
	partial_sum_t *part;
	bool need_join;
} worker_t;


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
		return NULL;
	}
	return part;
}

void *count_pi(void *arg) {
	partial_sum_t *part = (partial_sum_t*) arg;

	for (ulong i = part->startIdx; i < part->num_steps; i += part->num_threads) {
		*(part->sum) += 1.0 / ((double)i * 4.0 + 1.0);
		*(part->sum) -= 1.0 / ((double)i * 4.0 + 3.0);
	}
	return part;
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
	double standard_pi = 3.141592653589793;
	ulong num_steps = 5000000000;
	assert(num_threads > 0);

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
		worker->part->num_threads = num_threads;
		worker->part->num_steps = num_steps;
		worker->part->startIdx = i;

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

	pi = pi * 4.0;
	printf("Pi done:     %.15g \n", pi);
	printf("Standard Pi: %.15g\n", standard_pi);
	printf("Accuracy:    %.15g\n", standard_pi - pi);
	return 0;
}
