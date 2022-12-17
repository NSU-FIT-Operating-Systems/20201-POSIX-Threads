#ifndef HTTP_CPP_PROXY_IO_OPERATIONS_H
#define HTTP_CPP_PROXY_IO_OPERATIONS_H

#include <cstdlib>
#include <cstdio>
#include <string>

namespace io {
    static const int READ_PIPE_END = 0;
    static const int WRITE_PIPE_END = 1;
    static const int MSG_LENGTH_LIMIT = 32 * 1024;

    typedef struct message {
        const char *data;
        size_t len;
        size_t capacity;

        message() = default;

        ~message() {
            free((void *) data);
        }
    } message;

    message *copy(message *prev);

    size_t message_size(const message *message);

    bool append_msg(message *a, message *b);

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
