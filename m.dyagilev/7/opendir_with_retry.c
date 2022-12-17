#include <stddef.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>
#include <time.h>

#include "opendir_with_retry.h"


DIR* opendir_with_retry(size_t retry_period_nanoseconds,
						const char* name) {
	assert(NULL != name);
	struct timespec delay = {retry_period_nanoseconds / 1000000000,
							retry_period_nanoseconds % 1000000000};
	DIR* result = NULL;
	while (true) {
		result = opendir(name);
		if (NULL == result) {
			if ((EMFILE != errno) && (ENFILE != errno)) {
				return NULL;
			}
			nanosleep(&delay, NULL);
		}
		else {
			break;
		}
	}
	return result;
}

