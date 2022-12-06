#include "dirs.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>   /* mkdir(2) */
#include <errno.h>

static int maybe_mkdir(const char* path, mode_t mode) {
	struct stat st;
	errno = 0;

	/* Try to make the directory */
	if (mkdir(path, mode) == 0)
		return 0;

	/* If it fails for any reason but EEXIST, fail */
	if (errno != EEXIST)
		return -1;

	/* Check if the existing path is a directory */
	if (stat(path, &st) != 0)
		return -1;

	/* If not, fail with ENOTDIR */
	if (!S_ISDIR(st.st_mode)) {
		errno = ENOTDIR;
		return -1;
	}

	errno = 0;
	return 0;
}

int recursive_mkdir(const char *path){
	char* tempPath = NULL;
	char* p;
	int result = -1;
	mode_t mode = 0777;

	errno = 0;

	tempPath = strdup(path);
	if (tempPath == NULL)
		goto out;

	for (p = tempPath + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';

			if (maybe_mkdir(tempPath, mode) != 0)
				goto out;

			*p = '/';
		}
	}

	if (maybe_mkdir(tempPath, mode) != 0)
		goto out;

	result = 0;

out:
	free(tempPath);
	return result;
}