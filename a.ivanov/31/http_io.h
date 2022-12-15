#ifndef HTTP_CPP_PROXY_HTTP_IO_H
#define HTTP_CPP_PROXY_HTTP_IO_H

/*
 * reads as many bytes from file as possible,
 * but not more that MSG_LENGTH_LIMIT
 */
message *read_all(int socket_fd);



#endif //HTTP_CPP_PROXY_HTTP_IO_H
