#ifndef HTTP_CPP_PROXY_CACHE_H
#define HTTP_CPP_PROXY_CACHE_H

#include <string>
#include <functional>

namespace aiwannafly {
    template <class T>
    class cache {
    public:
        virtual ~cache() = default;;

        virtual T *get(const std::string&) = 0;

        virtual void erase(const std::string&) = 0;

        virtual bool contains(const std::string&) = 0;

        virtual bool put(const std::string&, T *value) = 0;

        virtual size_t size() = 0;

        virtual size_t size_bytes(std::function<size_t(T*)> func) = 0;

        virtual void clear() = 0;
    };
}

#endif //HTTP_CPP_PROXY_CACHE_H
