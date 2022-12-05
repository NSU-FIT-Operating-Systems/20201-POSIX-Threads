#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <stdbool.h>
#include <math.h>

enum ERRORS {
	MISSING_ARGUMENT = 1,
	INVALID_ARGUMENT,
};

#define ITERATIONS_NUMBER 200000000

typedef struct partial_sum_info {
	long long beginning_n;
	long long step;
	long long tail_step;
	long long tail_start_iteration;
	long long iterations_to_count;
} partial_sum_info;

long long calculate_beginning_n(long long thread_idx) {
	return (1 + thread_idx * 4);
}

long long calculate_step(long long thread_idx, long long threads_requested) {
	return (4 * threads_requested);
}

long long calculate_iterations_to_count(long long thread_idx,
										long long threads_requested, 
										long long base_iterations_per_thread,
										long long iterations_number) {
	return (threads_requested - 1 != thread_idx) ? base_iterations_per_thread :
			(iterations_number - base_iterations_per_thread * thread_idx);
}

void fill_work_division(partial_sum_info* info, long long threads_requested, 
						long long iterations_number) {
	assert(NULL != info);
	long long base_iterations_per_thread = iterations_number / threads_requested;
	for (size_t i = 0; i < threads_requested; ++i) {
		info[i].beginning_n = calculate_beginning_n(i);
		info[i].step = calculate_step(i, threads_requested);
		info[i].tail_step = 4;
		info[i].tail_start_iteration = base_iterations_per_thread;
		info[i].iterations_to_count = calculate_iterations_to_count(
													i, 
													threads_requested, 
													base_iterations_per_thread,
													iterations_number);
	}
}

void* partial_sum(void* arg) {
	assert(NULL != arg);
	partial_sum_info* sum_info = (partial_sum_info*)arg;
	long long cur_n = sum_info->beginning_n;
	double result = 0.0;
	for (size_t i = 0; i < sum_info->iterations_to_count; ++i) {
		result = result + 1.0 / cur_n - 1.0 / (cur_n + 2);
		if (sum_info->tail_start_iteration > i) {
			cur_n += sum_info->step;
		}
		else {
			cur_n += sum_info->tail_step;
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
		fprintf(stderr, "Need argument (threads number, not more than %d)\n", 
				ITERATIONS_NUMBER);
		pthread_exit((void*)MISSING_ARGUMENT);
	}

	char* remaining_string = NULL;
	long long threads_requested = strtoll(argv[1], &remaining_string, 10);
	if (('\0' != *remaining_string) || (0 > threads_requested) || 
		(ITERATIONS_NUMBER < threads_requested)) {
		fprintf(stderr, 
				"Invalid argument (expected integer between 0 and %d\n",
				ITERATIONS_NUMBER);
		pthread_exit((void*)INVALID_ARGUMENT);
	}

	partial_sum_info work_division[threads_requested];
	fill_work_division(work_division, threads_requested, ITERATIONS_NUMBER);
	
	pthread_t threads[threads_requested];
	size_t threads_num = 0;
	for (threads_num = 0; threads_num < threads_requested; ++threads_num) {
		int creation_result = pthread_create(&(threads[threads_num]), NULL, 
											partial_sum,
											&(work_division[threads_num]));
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

	if ((threads_num != threads_requested) || alloc_errors) {
		printf("Result is invalid due to errors\n");
	}

	double pi = 4 * sum_from_threads;

	printf("t_pi == %.15lf\n", pi);
	printf("M_PI == %.15lf\n", M_PI);
	printf("Difference == %.15lf\n", fabs(M_PI - pi));

	pthread_exit(0);
}
