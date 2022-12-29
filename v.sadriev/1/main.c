#include <stdio.h>
#include <pthread.h>

#define ITER_NUM 10

void *print_message(void * to_print) {
	for (int i = 0; i < ITER_NUM; ++i) {
		printf("%d: %s\n", i, (char *) to_print);
	}
	return NULL;
}

int main() {
	pthread_t newThread;
	char *new_thread_string = "New thread is cool!";
	char *main_thread_string = "Main(old) thread is cool(BASE)!";
	if (pthread_create(&newThread, NULL, &print_message, (void *) new_thread_string) != 0) {
		perror("Pthread creation fail");
		return -1;
	}

	for (int i = 0; i < ITER_NUM; ++i) {
		printf("%d: %s\n", i, main_thread_string);
	}

	pthread_exit(NULL);
}
