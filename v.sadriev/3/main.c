#include <stdio.h>
#include <pthread.h>
#include <assert.h>

#define NUM_THREADS 4
#define STR_PER_THREAD 10

void *print_strings(void *arg) {
	char **strings = (char**)arg;
	assert(strings != NULL);
	while (*strings != NULL) {
		printf("%s\n", *strings);
		++strings;
	}
	pthread_exit(NULL);
}

int main() {
	char *strings2[][STR_PER_THREAD + 1] = {{"t1:alalal", "t1:LULZ", "t1:LOOOOOOOOL", NULL},
	                                    {"t2:second", NULL},
	                                    {"t3:third", "t3:STRING", NULL},
	                                    {"t4:kek", NULL}};
	pthread_t threads[NUM_THREADS];
	for (int i = 0; i < NUM_THREADS; ++i) {
		if (pthread_create(&threads[i], NULL, &print_strings, (void*)strings2[i]) != 0) {
			perror("Couldn't create thread");
			pthread_exit(NULL);
		}
	}

	for (int i = 0; i < NUM_THREADS; ++i) {
		if (pthread_join(threads[i], NULL) != 0) {
			perror("Couldn't join thread");
			pthread_exit(NULL);
		}
	}
	return 0;
}
