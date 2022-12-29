#include "paths_pair.h"
#include <stdlib.h>

void free_paths_pair(paths_pair* pair) {
	free(pair->src_path);
	free(pair->dst_path);
	free(pair);
}
