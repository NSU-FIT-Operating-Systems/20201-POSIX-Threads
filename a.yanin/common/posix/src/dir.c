#include "common/posix/dir.h"

#include <assert.h>
#include <fcntl.h>

posix_err_t wrapper_opendir(char const *name, DIR **result) {
    assert(name != NULL);
    assert(result != NULL);

    errno = 0;
    DIR *dir = opendir(name);

    if (dir == NULL) {
        return make_posix_err("opendir(3) failed");
    }

    *result = dir;

    return make_posix_err_ok();
}

posix_err_t wrapper_fdopendir(int fd, DIR **result) {
    assert(fd >= 0 || fd == AT_FDCWD);
    assert(result != NULL);

    errno = 0;
    DIR *dir = fdopendir(fd);

    if (dir == NULL) {
        return make_posix_err("fdopendir(3) failed");
    }

    *result = dir;

    return make_posix_err_ok();
}

posix_err_t wrapper_readdir(DIR *dir, struct dirent **result) {
    assert(dir != NULL);
    assert(result != NULL);

    errno = 0;
    struct dirent *dirent = readdir(dir);

    if (dirent == NULL && errno != 0) {
        return make_posix_err("readdir(3) failed");
    }

    *result = dirent;

    return make_posix_err_ok();
}

posix_err_t wrapper_closedir(DIR *dir) {
    assert(dir != NULL);

    errno = 0;

    if (closedir(dir) < 0) {
        return make_posix_err("closedir(3) failed");
    }

    return make_posix_err_ok();
}
