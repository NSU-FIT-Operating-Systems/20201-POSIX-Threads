#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#define MAX_STRINGS_AMT 100
#define NS_TO_MS 1000000 // reminder: 1ns = 1e-6 ms
#define SLEEP_PER_CHAR_NS 10 * NS_TO_MS

typedef struct string_t {
	size_t len;
	char* buf; // null bytes my beloved
} String;

void freeString(String* what) {
	free(what->buf);
}

pthread_barrier_t barrier;

void* threadFn_sort(void* arg) {
	pthread_barrier_wait(&barrier);

	String* str = (String*)arg;

	struct timespec ts; // incredibly annoying to use
	int res;

	long nanoseconds = SLEEP_PER_CHAR_NS * str->len;

	ts.tv_sec = nanoseconds / (NS_TO_MS * 1000);
	ts.tv_nsec = nanoseconds % (NS_TO_MS * 1000);

	do { res = nanosleep(&ts, &ts); }
		while (res && errno == EINTR);

	printf("%s", str->buf);

	return NULL;
}

int main() {
	String** lines = malloc(sizeof(String) * MAX_STRINGS_AMT);
	size_t curLines = 0;

	const bool isTerminal = isatty(1);

	size_t worthless = 0; // we'll pass this into getline since we don't need the second argument

	if (isTerminal) {
		printf("You should type text... NOW!\n"); // admittedly not even funny
	}

	while (1) {
		String* str = malloc(sizeof(String));
		str->buf = NULL;	// If *lineptr is set to NULL before the call, then getline() will allocate a buffer for storing the line.
							// This buffer should be freed by the user program even if getline() failed.

		ssize_t ok = getline(&str->buf, &worthless, stdin);
		str->len = ok;

		// On success, getline() and getdelim() return the number of characters read, including the delimiter character (but not "\0")

		if (ok < 0) { // error
			if (isTerminal) {
				perror("getline died: ");
			}
			free(str->buf);
			break;
		} else if (ok <= 1) { // empty line
			if (isTerminal) {
				printf("Empty line encountered; interpreting as end of input.\n");
			}
			break;
		}

		// line; add to list
		lines[curLines] = str;
		curLines++;

		if (curLines == MAX_STRINGS_AMT) {
			if (isTerminal) {
				printf("Strings limit reached.\n");
			}
			break;
		}
	}

	if (isTerminal) {
		printf("Sleep-sorting %lu strings...\n", curLines);
	}

	pthread_barrier_init(&barrier, NULL, curLines + 1);

	pthread_t xd[curLines];

	for (int i = 0; i < curLines; i++) {
		pthread_create(&xd[i], NULL, threadFn_sort, lines[i]);
	}

	// wait to decrease barrier counter

	pthread_barrier_wait(&barrier);
	// second wait

	for (int i = 0; i < curLines; i++) {
		pthread_join(xd[i], 0);
	}

	for (int i = 0; i < curLines; i++) {
		freeString(lines[i]);
	}

	free(lines);

	if (isTerminal) {
		printf("-- Done; exiting. --\n");
	}
	pthread_exit(NULL);
}