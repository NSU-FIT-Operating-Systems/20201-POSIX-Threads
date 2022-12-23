#ifndef OPEN_WITH_RETRY
#define OPEN_WITH_RETRY

#include <stddef.h>
#include <fcntl.h>

int open_with_retry(size_t retry_period_nanoseconds,
					const char* name, int flags, mode_t mode);

#endif
