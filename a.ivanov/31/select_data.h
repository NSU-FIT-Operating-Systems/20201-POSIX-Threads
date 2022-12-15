#ifndef HTTP_CPP_PROXY_SELECT_DATA_H
#define HTTP_CPP_PROXY_SELECT_DATA_H

#include <cstdlib>

namespace io_operations {
    class select_data {
    public:
        enum fd_type {
            READ, WRITE
        };

        select_data() {
            FD_ZERO(read_set);
            FD_ZERO(write_set);
        }

        ~select_data() {
            delete read_set;
            delete write_set;
        }

        void add_fd(int fd, fd_type type) {
            if (type == READ) {
                FD_SET(fd, read_set);
            } else {
                FD_SET(fd, write_set);
            }
            if (fd > max_fd) {
                max_fd = fd;
            }
        }

        void remove_fd(int fd, fd_type type) {
            if (type == READ) {
                FD_CLR(fd, read_set);
                if (fd == max_fd) {
                    max_fd--;
                }
            } else {
                FD_CLR(fd, write_set);
            }
        }

        [[nodiscard]] fd_set *get_read_set() const {
            return read_set;
        }

        [[nodiscard]] fd_set *get_write_set() const {
            return write_set;
        }

        [[nodiscard]] int get_max_fd() const {
            return max_fd;
        }

    private:
        int max_fd = 0;
        fd_set *read_set = new fd_set;
        fd_set *write_set = new fd_set;
    };
}

#endif //HTTP_CPP_PROXY_SELECT_DATA_H
