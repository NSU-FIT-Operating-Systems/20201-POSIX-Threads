#ifndef OPENDIR_WITH_RETRY_H
#define OPENDIR_WITH_RETRY_H

#include <dirent.h>

DIR* opendir_with_retry(size_t retry_period_nanoseconds,
						const char* name);

#endif
