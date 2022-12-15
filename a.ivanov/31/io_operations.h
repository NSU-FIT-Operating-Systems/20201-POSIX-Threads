#ifndef HTTP_CPP_PROXY_IO_OPERATIONS_H
#define HTTP_CPP_PROXY_IO_OPERATIONS_H

#include <cstdlib>
#include <cstdio>
#include <string>

namespace io_operations {
    static const int READ_PIPE_END = 0;
    static const int WRITE_PIPE_END = 1;
    static const int MSG_LENGTH_LIMIT = 32 * 1024;

    typedef struct message {
        const char *data;
        size_t len;

        message() = default;

        message(const std::string& s) {
            data = s.data();
            len = s.length();
        }

        ~message() {
            free((void *) data);
        }
    } message;

    message *concat_messages(message *a, message *b);

    bool write_all(int fd, message *message);

    bool fwrite_into_pipe(FILE *pipe_fd, char *buffer, size_t len);

    /*
     * reads as many bytes from file as possible,
     * but not more that MSG_LENGTH_LIMIT
     */
    message *read_all(int socket_fd);

    char *read_from_file(int pipe_fd);

    char *fread_from_pipe(FILE *pipe_fp);
}

#endif //HTTP_CPP_PROXY_IO_OPERATIONS_H
