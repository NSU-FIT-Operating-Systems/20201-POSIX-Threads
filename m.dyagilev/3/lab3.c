#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>

#define SEQ_NUM 4
#define ARRS_LEN 3

enum ERRORS {
	ALLOCATION_ERROR = 1,
	CREATE_ERROR,
	JOIN_ERROR,
};

typedef struct strarr {
	char** arr;
	size_t len;
} strarr;

void* print_strarr(void* arg) {
	assert(NULL != arg);
	strarr* arr = (strarr*)arg;
	for (size_t i = 0; i < arr->len; ++i) {
		printf("%s\n", arr->arr[i]);
	}
	pthread_exit(0);
}

void free_strarr(strarr strarr) {
	for (size_t i = 0; i < strarr.len; ++i) {
		free((strarr.arr)[i]);
	}
	free(strarr.arr);
}

int fill_strarrs(strarr* strarrs, size_t strarrs_num, size_t strs_num) {
	assert(NULL != strarrs);
	for (size_t i = 0; i < strarrs_num; ++i) {
		strarrs[i].arr = NULL;
		strarrs[i].len = 0;
		char** cur_arr = calloc(strs_num, sizeof(char*));
		if (NULL == cur_arr) {
			for (size_t j = 0; j < i; ++j) {
				free_strarr(strarrs[j]);
			}
			return ALLOCATION_ERROR;
		}
		strarrs[i].arr = cur_arr;
		for (size_t j = 0; j < strs_num; ++j) {
			char* new_str = calloc(strlen("String i j") + (i / 10) + (j / 10)
									+ 1, sizeof(char));
			if (NULL == new_str) {
				for (size_t k = 0; k < i; ++k) {
					free_strarr(strarrs[k]);
				}
				return ALLOCATION_ERROR;
			}
			sprintf(new_str, "String %zu %zu", i, j);
			cur_arr[j] = new_str;
			(strarrs[i].len)++;
		}
	}
	return 0;
}

int main() {
	strarr arrs[SEQ_NUM];
	if (fill_strarrs(arrs, SEQ_NUM, ARRS_LEN)) {
		pthread_exit((void*)ALLOCATION_ERROR);
	}
	pthread_t threads[SEQ_NUM];
	size_t threads_num = 0;
	for (threads_num = 0; threads_num < SEQ_NUM; ++threads_num) {
		int creation_result = pthread_create(&(threads[threads_num]), NULL, 
											print_strarr, 
											&(arrs[threads_num]));
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
	for (size_t i = 0; i < SEQ_NUM; ++i) {
		free_strarr(arrs[i]);
	}
	pthread_exit(0);
}
