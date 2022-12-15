#ifndef HTTP_CPP_PROXY_PROXY_H
#define HTTP_CPP_PROXY_PROXY_H

class proxy {
public:

    virtual ~proxy() = default;

    virtual void run(int port) = 0;

    virtual void shutdown() = 0;
};

#endif //HTTP_CPP_PROXY_PROXY_H
