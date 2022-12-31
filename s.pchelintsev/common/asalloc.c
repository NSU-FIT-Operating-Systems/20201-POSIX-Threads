#include <stdlib.h>
#include <stdio.h>

#include <pthread.h>

// DOWN WITH THE ASSERTS

void* asmalloc(size_t sz) {
	void* ret = malloc(sz);
	if (ret == NULL) {
		printf("[ERROR] Failed to allocate %d bytes, exiting.\n", sz);
		pthread_exit(NULL);
	}
	return ret;
}

void* ascalloc(size_t amt, size_t sz) {
	void* ret = calloc(amt, sz);
	if (ret == NULL) {
		printf("[ERROR] Failed to allocate %d bytes, exiting.\n", sz);
		pthread_exit(NULL);
	}

	return ret;
}

void* asrealloc(void* ptr, size_t sz) {
	void* ret = realloc(ptr, sz);
	if (ret == NULL) {
		printf("[ERROR] Failed to reallocate %d bytes, exiting.\n", sz);
		pthread_exit(NULL);
	}

	return ret;
}
