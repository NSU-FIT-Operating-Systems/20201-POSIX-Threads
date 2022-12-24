#ifndef HTTP_CPP_PROXY_RUNNABLE_H
#define HTTP_CPP_PROXY_RUNNABLE_H

class Runnable {
public:

    virtual ~Runnable() = default;

    virtual void run(int port) = 0;
};

#endif //HTTP_CPP_PROXY_RUNNABLE_H
