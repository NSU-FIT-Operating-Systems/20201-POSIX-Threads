#include "common/posix/time.h"

#include <assert.h>
#include <string.h>

posix_err_t wrapper_nanosleep(struct timespec *const time) {
    assert(time != NULL);

    int retval = -1;
    struct timespec remaining;
    memcpy(&remaining, time, sizeof(remaining));

    do {
        errno = 0;
        retval = nanosleep(&remaining, &remaining);
    } while (retval < 0 && errno == EINTR);

    if (retval < 0) {
        return make_posix_err("nanosleep(2) failed");
    }

    return make_posix_err_ok();
}

posix_err_t wrapper_clock_gettime(clockid_t clock_id, struct timespec *tp) {
    assert(tp != NULL);

    struct timespec result = {0};
    errno = 0;
    int retval = clock_gettime(clock_id, &result);

    if (retval < 0) {
        return make_posix_err("clock_gettime(2) failed");
    }

    memcpy(tp, &result, sizeof(struct timespec));

    return make_posix_err_ok();
}
