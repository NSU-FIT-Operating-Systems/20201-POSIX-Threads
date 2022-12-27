#include "paths.h"
#include <stdlib.h>
#include <string.h>

paths_t *create_paths(char *src, char *dst) {
	if (src == NULL || dst == NULL) {
		return NULL;
	}
	paths_t *paths = (paths_t *)malloc(sizeof(*paths));
	if (paths == NULL) {
		return NULL;
	}
	paths->src = src;
	paths->dst = dst;
	return paths;
}

void free_paths(paths_t *paths) {
	if (paths != NULL) {
		if (paths->src != NULL) {
			free(paths->src);
		}
		if (paths->dst != NULL) {
			free(paths->dst);
		}
		free(paths);
	}
}

