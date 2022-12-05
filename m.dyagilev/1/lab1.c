#include <stdio.h>
#include <pthread.h>

enum ERRORS {
	CREATE_ERROR = 1,
};

void* func(void* args) {
	printf("f1\n");
	printf("f2\n");
	printf("f3\n");
	printf("f4\n");
	printf("f5\n");
	pthread_exit(0);
}

int main() {
	pthread_t thread;

	int creation_result = pthread_create(&thread, NULL, func, NULL);
	if (0 != creation_result) {
		fprintf(stderr, "Main error: can't create thread, result = %d\n", 
				creation_result);
		pthread_exit((void*)CREATE_ERROR);
	}

	printf("m1\n");
	printf("m2\n");
	printf("m3\n");
	printf("m4\n");
	printf("m5\n");

	pthread_exit(0);
}
