#ifndef HTTP_CPP_PROXY_SELECT_DATA_H
#define HTTP_CPP_PROXY_SELECT_DATA_H

#include <cstdlib>
#include <cassert>

namespace io {
    class SelectData {
    public:
        enum fd_type {
            READ, WRITE
        };

        SelectData() {
            FD_ZERO(read_set);
            FD_ZERO(write_set);
            rwlock = new pthread_rwlock_t;
            int code = pthread_rwlock_init(rwlock, nullptr);
            assert(code == 0);
        }

        ~SelectData() {
            delete read_set;
            delete write_set;
            pthread_rwlock_destroy(rwlock);
            delete rwlock;
        }

        void addFd(int fd, fd_type type) {
            pthread_rwlock_wrlock(rwlock);
            if (type == READ) {
                FD_SET(fd, read_set);
            } else {
                FD_SET(fd, write_set);
            }
            if (fd > max_fd) {
                max_fd = fd;
            }
            pthread_rwlock_unlock(rwlock);
        }

        void remove_fd(int fd, fd_type type) {
            pthread_rwlock_wrlock(rwlock);
            if (type == READ) {
                FD_CLR(fd, read_set);
                if (fd == max_fd) {
                    max_fd--;
                }
            } else {
                FD_CLR(fd, write_set);
            }
            pthread_rwlock_unlock(rwlock);
        }

        [[nodiscard]] fd_set *getReadSet() const {
            pthread_rwlock_rdlock(rwlock);
            auto res = read_set;
            pthread_rwlock_unlock(rwlock);
            return res;
        }

        [[nodiscard]] fd_set *getWriteSet() const {
            pthread_rwlock_rdlock(rwlock);
            auto res = write_set;
            pthread_rwlock_unlock(rwlock);
            return res;
        }

        [[nodiscard]] int getMaxFd() const {
            pthread_rwlock_rdlock(rwlock);
            auto res = max_fd;
            pthread_rwlock_unlock(rwlock);
            return res;
        }

    private:
        int max_fd = 0;
        fd_set *read_set = new fd_set;
        fd_set *write_set = new fd_set;
        pthread_rwlock_t *rwlock;
    };
}

#endif //HTTP_CPP_PROXY_SELECT_DATA_H
