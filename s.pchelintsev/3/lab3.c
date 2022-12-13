#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

const int Threads = 4;

void* threadFn(void* a) {
	char* string = (char*)a;
	printf("%s\n", string);

	return NULL;
}

int main() {
	pthread_t thread_id[Threads];

	char strings[Threads][64];

	for (int str = 0; str < Threads; str++) {
		snprintf((char*)strings[str], sizeof(strings[str]),
			"Print from thread #%d", str);
	}

	for (int thread = 0; thread < Threads; thread++) {
		int ok = pthread_create(&thread_id[thread], NULL, threadFn, strings[thread]);
		if (ok != 0) {
			perror("Failed to create a thread. ");
			return -1;
		}
	}

	// join all threads we created so they don't use deallocated `strings`
	for (int thread = 0; thread < Threads; thread++) {
		if (pthread_join(thread_id[thread], NULL) != 0) {
			perror("pthread_join failed (HOW\nBOTTOM TEXT): ");
		}
	}
}