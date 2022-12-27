#ifndef PATHS_PAIR_H
#define PATHS_PAIR_H

typedef struct paths_pair {
	char* src_path;
	char* dst_path;
} paths_pair;

void free_paths_pair(paths_pair* pair);

#endif
