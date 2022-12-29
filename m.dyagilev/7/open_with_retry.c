#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <time.h>

#include "open_with_retry.h"

int open_with_retry(size_t retry_period_nanoseconds,
					const char* name, int flags, mode_t mode) {
	assert(NULL != name);
	struct timespec delay = {retry_period_nanoseconds / 1000000000,
							retry_period_nanoseconds % 1000000000};
	int result = 0;
	while (true) {
		result = open(name, flags, mode);
		if (0 > result) {
			if ((EMFILE != errno) && (ENFILE != errno)) {
				return result;
			}
			nanosleep(&delay, NULL);
		}
		else {
			break;
		}
	}
	return result;
}
