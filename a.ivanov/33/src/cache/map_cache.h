#ifndef HTTP_CPP_PROXY_MAP_CACHE_H
#define HTTP_CPP_PROXY_MAP_CACHE_H

#include <map>
#include <cassert>
#include <pthread.h>

#include "cache.h"

template<class T>
class MapCache final : public Cache<T> {
public:

    MapCache() {
        this->table = new std::map<std::string, T *>();
        rwlock = new pthread_rwlock_t;
        int code = pthread_rwlock_init(rwlock, nullptr);
        assert(code == 0);
        mutex = new pthread_mutex_t;
        code = pthread_mutex_init(mutex, nullptr);
        assert(code == 0);
    }

    ~MapCache() {
        delete table;
        pthread_rwlock_destroy(rwlock);
        delete rwlock;
        pthread_mutex_destroy(mutex);
        delete mutex;
    }

    void lock() override {
        pthread_mutex_lock(mutex);
    }

    void unlock() override {
        pthread_mutex_unlock(mutex);
    }

    T *get(const std::string &key) override {
        pthread_rwlock_rdlock(rwlock);
        auto *res = table->at(key);
        pthread_rwlock_unlock(rwlock);
        return res;
    }

    void erase(const std::string &key) override {
        pthread_rwlock_wrlock(rwlock);
        table->erase(key);
        pthread_rwlock_unlock(rwlock);
    }

    bool contains(const std::string &key) override {
        pthread_rwlock_rdlock(rwlock);
        bool res = table->contains(key);
        pthread_rwlock_unlock(rwlock);
        return res;
    }

    bool put(const std::string &key, T *value) override {
        pthread_rwlock_wrlock(rwlock);
        (*table)[key] = value;
        pthread_rwlock_unlock(rwlock);
        return true;
    };

    size_t size() override {
        pthread_rwlock_rdlock(rwlock);
        auto res = table->size();
        pthread_rwlock_unlock(rwlock);
        return res;
    }

    size_t sizeBytes(std::function<size_t(const T *)> size) override {
        pthread_rwlock_rdlock(rwlock);
        size_t total_size = 0;
        for (auto it = table->begin(); it != table->end(); it++) {
            total_size += size(it->second);
        }
        pthread_rwlock_unlock(rwlock);
        return total_size;
    }

    void clear() override {
        pthread_rwlock_wrlock(rwlock);
        for (auto it = table->begin(); it != table->end(); it++) {
            delete it->second;
        }
        table->clear();
        pthread_rwlock_unlock(rwlock);
    }

private:
    std::map<std::string, T *> *table;
    pthread_rwlock_t *rwlock{};
    pthread_mutex_t *mutex;
};

#endif //HTTP_CPP_PROXY_MAP_CACHE_H
