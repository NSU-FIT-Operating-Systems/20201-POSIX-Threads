#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <stdbool.h>
#include <signal.h>
#include <math.h>

enum ERRORS {
	MISSING_ARGUMENT = 1,
	INVALID_ARGUMENT,
};

typedef struct work_info {
	size_t beginning_iteration;
	size_t iterations_to_count;
} work_info;

bool interrupted = false;

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
size_t iterations_queue = 0;

#define INTERRUPTION_CHECK_PERIOD 1000000

#define COUNT_BLOCK 2000000

void signal_handler(int arg) {
	interrupted = true;
}

void fill_work_division(work_info* result) {
	assert(NULL != result);
	if (pthread_mutex_lock(&queue_mutex)) {
		result->iterations_to_count = 0;
		return;
	}
	result->beginning_iteration = iterations_queue;
	result->iterations_to_count = COUNT_BLOCK;
	iterations_queue += COUNT_BLOCK;
	if (pthread_mutex_unlock(&queue_mutex)) {
		result->iterations_to_count = 0;
	}
}

long long get_nth_block_fraction_denominator(size_t n) {
	return 1 + 4 * n;
}

void* partial_sum(void* arg) {
	work_info current_work_info = {0};
	double result = 0.0;
	size_t iterations_done = 0;
	while (true) {
		fill_work_division(&current_work_info);
		long long cur_n = 
			get_nth_block_fraction_denominator(current_work_info.beginning_iteration);
		bool stopped = false;
		for (size_t i = 0; i < current_work_info.iterations_to_count; ++i){
			result = result + 1.0 / cur_n - 1.0 / (cur_n + 2);
			cur_n += 4;
			iterations_done++;
			if (0 == iterations_done % INTERRUPTION_CHECK_PERIOD) {
				if (interrupted) {
					stopped = true;
					break;
				}
			}
		}
		if (stopped) {
			break;
		}
	}
	double* result_allocated = (double*)malloc(sizeof(double));
	if (NULL != result_allocated) {
		*result_allocated = result;
	}
	pthread_exit(result_allocated);
}

int main(int argc, char** argv) {
	if (2 > argc) {
		fprintf(stderr, "Need argument (threads number)\n");
		pthread_exit((void*)MISSING_ARGUMENT);
	}

	char* remaining_string = NULL;
	long long threads_requested = strtoll(argv[1], &remaining_string, 10);
	if (('\0' != *remaining_string) || (0 > threads_requested)) {
		fprintf(stderr, 
				"Invalid argument (expected positive integer)\n");
		pthread_exit((void*)INVALID_ARGUMENT);
	}
	
	signal(SIGINT, signal_handler);

	pthread_t threads[threads_requested];
	size_t threads_num = 0;
	for (threads_num = 0; threads_num < threads_requested; ++threads_num) {
		int creation_result = pthread_create(&(threads[threads_num]), NULL,
											partial_sum, NULL);
		if (0 != creation_result) {
			fprintf(stderr, "Main error: can't create thread for %zu, result = %d\n", 
					threads_num , creation_result);
			break;
		}
	}

	bool alloc_errors = false;
	double sum_from_threads = 0.0;
	for (size_t i = 0; i < threads_num; ++i) {
		double* partial_result;
		int join_result = pthread_join(threads[i], (void**)&partial_result);
		if (0 != join_result) {
			fprintf(stderr, "Main error: can't join thread for %zu, result = %d\n", 
					i, join_result);
		}
		if (NULL != partial_result) {
			sum_from_threads += *partial_result;
			free(partial_result);
		}
		else {
			alloc_errors = true;
		}
	}
	pthread_mutex_destroy(&queue_mutex);

	if (threads_num != threads_requested) {
		printf("Had only %zu of %lld threads working\n", threads_num,
														threads_requested);
	}

	if (alloc_errors) {
		printf("Result is invalid due to errors\n");
	}

	double pi = 4 * sum_from_threads;

	printf("\nt_pi == %.15lf\n", pi);
	printf("M_PI == %.15lf\n", M_PI);
	printf("Difference == %.15lf\n", fabs(M_PI - pi));

	pthread_exit(0);
}
