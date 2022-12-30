#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#define ITER_NUM 10
#define TIME_TO_SLEEP 3


void print_before_cancel() {
	printf("I have been canceled! :( cancel culture is suck\n");
}

void *print_message(void * to_print) {
	pthread_cleanup_push(&print_before_cancel, NULL);
	for (int i = 0; i < ITER_NUM; ++i) {
		printf("%s\n", (char *) to_print);
		sleep(1);
	}
	pthread_cleanup_pop(0);
	return NULL;
}

int main() {
	pthread_t new_thread;
	char *new_thread_string = "New thread is cool!";
	void *res;
	int err = pthread_create(&new_thread, NULL, &print_message, (void *) new_thread_string);
	if (err != 0) {
		perror("Pthread creation fail");
		pthread_exit(&err);
	}

	sleep(TIME_TO_SLEEP);

	err = pthread_cancel(new_thread);
	if (err != 0) {
		perror("Cancellation error");
		pthread_exit(&err);
	}

	err = pthread_join(new_thread, &res);
	if (err != 0) {
		perror("Couldn't join threads");
		pthread_exit(&err);
	}

	if (res == PTHREAD_CANCELED) {
		printf("New thread has been canceled\n");
	} else {
		printf("New thread hasn't been canceled\n");
	}

	pthread_exit(NULL);
}
