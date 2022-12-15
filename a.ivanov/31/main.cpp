#include <iostream>

#include "proxy.h"
#include "single_thread_proxy.h"

int main() {
    proxy *p = new single_thread_proxy::http_proxy();
    p->run(5000);
    delete p;
    return 0;
}
