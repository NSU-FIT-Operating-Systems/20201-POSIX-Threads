#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

enum ERRORS {
	CREATE_ERROR = 1,
	CANCEL_ERROR,
	JOIN_ERROR,
};

void* func(void* args) {
	while (1) {
		printf("f");
	}
}

int main() {
	pthread_t thread;
	
	int creation_result = pthread_create(&thread, NULL, func, NULL);
	if (0 != creation_result) {
		fprintf(stderr, "Main error: can't create thread, result = %d\n", 
				creation_result);
		pthread_exit((void*)CREATE_ERROR);
	}

	sleep(2);

	int cancellation_result = pthread_cancel(thread);
	if (0 != cancellation_result) {
		fprintf(stderr, "Main error: can't cancel thread, result = %d\n",
				cancellation_result);
		pthread_exit((void*)CANCEL_ERROR);
	}

	long status = 0;
	int join_result = pthread_join(thread, (void**)&status);
	if (0 != join_result) {
		fprintf(stderr, "Main error: can't join thread, result = %d\n", 
				join_result);
		pthread_exit((void*)JOIN_ERROR);
	}
	if (PTHREAD_CANCELED != (void*)status) {
		fprintf(stderr, "Main error: thread was not cancelled?\n");
		pthread_exit((void*)CANCEL_ERROR);
	}

	printf("\nSuccess\n");

	pthread_exit(0);
}
