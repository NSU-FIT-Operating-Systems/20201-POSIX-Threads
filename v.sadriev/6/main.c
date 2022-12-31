#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define BUF_SIZE 128
#define MAX_STRINGS 100
#define SCALE 8
#define ONE_MILISEC 1000

// 1000 и 8 норм

void *print_string(void *string) {
	char *str = (char*)string;
	size_t len = strlen(str);
	size_t sleep_time = len * SCALE * ONE_MILISEC;
	usleep(sleep_time);
	printf("%s", str);
	pthread_exit(NULL);
}

int main() {
	FILE *f = fopen("strings.txt", "r");
	if (f == NULL) {
		perror("Couldn't open the file");
		return -1;
	}

	char *strings[MAX_STRINGS];
	pthread_t threads[MAX_STRINGS];
	char buf[BUF_SIZE];
	int i = 0;
	while (fgets(buf, BUF_SIZE, f) != NULL) {
		char *thread_str = malloc(BUF_SIZE * sizeof(char));
		if (thread_str == NULL) {
			fprintf(stderr, "Couldn't allocate string for thread\n");
			pthread_exit(&i);
		}
		strncpy(thread_str, buf, BUF_SIZE);
		strings[i] = thread_str;
		if (pthread_create(&threads[i], NULL, &print_string, thread_str) != 0) {
			perror("Couldn't create thread");
			pthread_exit(&i);
		}
		++i;
	}

	for (int j = 0; j < i; ++j) {
		if (pthread_join(threads[j], NULL) != 0) {
			perror("Couldn't join thread");
			for (int k = 0; k < j; ++k) {
				free(strings[k]);
			}
			pthread_exit(NULL);
		}
		free(strings[j]);
	}

	return 0;
}
