#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

#define MAX_LINES 100
#define DELAY_FOR_SYMBOL_NANOSECONDS 4000000

enum ERRORS {
	READ_ERROR = 1,
	CREATE_ERROR,
};

typedef struct line {
	char* str;
	size_t len;
} line;

void* sleepsort_core(void* arg) {
	assert(NULL != arg);
	line* cur_line = (line*)arg;
	size_t delay = DELAY_FOR_SYMBOL_NANOSECONDS * cur_line->len;
	struct timespec req = {delay / 1000000000, delay % 1000000000};
	nanosleep(&req, NULL);
	printf("%s\n", cur_line->str);
	pthread_exit(0);
}

void free_arr(line* arr, size_t arr_len) {
	assert(NULL != arr);
	for (size_t i = 0; i < arr_len; ++i) {
		free((arr[i]).str);
	}
}

int main() {
	line lines[MAX_LINES];
	size_t lines_num = 0;
	bool error = false;
	while (true) {
		lines[lines_num].str = NULL;
		lines[lines_num].len = 0;
		ssize_t getline_result = 0;
		if (0 > (getline_result = getline(&(lines[lines_num].str),
										&(lines[lines_num].len), stdin))) {
			if (!feof(stdin)) {
				error = true;
			}
			break;
		}
		lines[lines_num].len = getline_result;
		lines_num++;
	}
	if (error) {
		free_arr(lines, lines_num + 1); //The buffer must be freed even if getline() failed
		return READ_ERROR;
	}
	pthread_t threads[lines_num];
	size_t threads_num = 0;
	for (threads_num = 0; threads_num < lines_num; ++threads_num) {
		int creation_result = pthread_create(&(threads[threads_num]), NULL,
											sleepsort_core, 
											&(lines[threads_num]));
		if (0 != creation_result) {
			fprintf(stderr, "Main error: can't create thread for %zu, result = %d\n", 
					threads_num , creation_result);
			break;
		}
	}
	for (size_t i = 0; i < threads_num; ++i) {
		long status;
		int join_result = pthread_join(threads[i], (void**)&status);
		if (0 != join_result) {
			fprintf(stderr, "Main error: can't join thread for %zu, result = %d\n", 
					i, join_result);
		}
	}
	free_arr(lines, lines_num + 1);
	pthread_exit(0);
}
