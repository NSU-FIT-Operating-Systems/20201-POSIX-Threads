#ifndef PTHREAD_HTTP_PROXY_THREADSAFE_LIST_H
#define PTHREAD_HTTP_PROXY_THREADSAFE_LIST_H

#include <cassert>
#include <cstddef>
#include <vector>
#include <pthread.h>

template <class T>
class ThreadsafeList {
public:
    explicit ThreadsafeList() {
        vec = std::vector<T>();
        rwlock = new pthread_rwlock_t;
        int code = pthread_rwlock_init(rwlock, nullptr);
        assert(code == 0);
    }

    ~ThreadsafeList() {
        pthread_rwlock_destroy(rwlock);
        delete rwlock;
    };

    void push_back(const T &in) {
        pthread_rwlock_wrlock(rwlock);
        vec.push_back(in);
        pthread_rwlock_unlock(rwlock);
    }

    T &operator[](const int index) {
        pthread_rwlock_rdlock(rwlock);
        auto res = vec[index];
        pthread_rwlock_unlock(rwlock);
        return res;
    }

    T &at(const int index) {
        pthread_rwlock_rdlock(rwlock);
        auto res = vec[index];
        pthread_rwlock_unlock(rwlock);
        return res;
    }

    T &front() {
        pthread_rwlock_rdlock(rwlock);
        auto res = vec.front();
        pthread_rwlock_unlock(rwlock);
        return res;
    }

    size_t size() {
        pthread_rwlock_rdlock(rwlock);
        auto res = vec.size();
        pthread_rwlock_unlock(rwlock);
        return res;
    }

    bool empty() {
        pthread_rwlock_rdlock(rwlock);
        auto res = vec.empty();
        pthread_rwlock_unlock(rwlock);
        return res;
    }

    void erase(typename std::vector<T>::iterator index) {
        pthread_rwlock_wrlock(rwlock);
        vec.erase(index);
        pthread_rwlock_unlock(rwlock);
    }

    void clear() {
        pthread_rwlock_wrlock(rwlock);
        vec.clear();
        pthread_rwlock_unlock(rwlock);
    }

    typename std::vector<T>::iterator begin(){
        pthread_rwlock_rdlock(rwlock);
        auto res = vec.begin();
        pthread_rwlock_unlock(rwlock);
        return res;
    }

    typename std::vector<T>::iterator end() {
        pthread_rwlock_rdlock(rwlock);
        auto res = vec.end();
        pthread_rwlock_unlock(rwlock);
        return res;
    }

private:
    std::vector<T> vec;
    pthread_rwlock_t *rwlock{};
};


#endif //PTHREAD_HTTP_PROXY_THREADSAFE_LIST_H
