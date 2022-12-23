#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "append_name_to_path.h"

char* append_name_to_path(const char* path, const char* name) {
	assert(NULL != path);
	assert(NULL != name);
	size_t path_len = strlen(path);
	size_t name_len = strlen(name);
	char* new_path = malloc(path_len + (('/' == path[path_len - 1]) ? 0 : 1) + 
							name_len + 1 /* '\0' */);
	if (NULL == new_path) {
		return NULL;		
	}
	strcpy(new_path, path);
	if ('/' != new_path[strlen(path) - 1]) {
		strcat(new_path, "/");
	}
	return strcat(new_path, name);
}
