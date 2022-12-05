#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum ERRORS {
	CANNOT_OPEN_FILE = 1,
};

#define LINES_NUM 100
#define LINE_LEN_MIN 1
#define LINE_LEN_MAX 80
#define OUTPUT_FILENAME "lines.txt"

int main() {
	FILE* output = fopen(OUTPUT_FILENAME, "w");
	if (NULL == output) {
		fprintf(stderr, "Cannot open/create output file\n");
		return CANNOT_OPEN_FILE;
	}

	srand(time(NULL));
	for (size_t i = 0; i < LINES_NUM; ++i) {
		int len = (rand() % (LINE_LEN_MAX - LINE_LEN_MIN + 1)) + LINE_LEN_MIN;
		for (size_t j = 0; j < len; ++j) {
			fprintf(output, "%c", (rand() % ('Z' - 'A' + 1)) + 'A');
		}
		fprintf(output, "\n");
	}
	fclose(output);
}
