#ifndef LAB7__PATHS_H_
#define LAB7__PATHS_H_

typedef struct Paths {
	char *src;
	char *dst;
} paths_t;

void free_paths(paths_t *paths);
paths_t *create_paths(char *src, char *dst);
#endif //LAB7__PATHS_H_
