#ifndef HTTP_CPP_PROXY_MAP_CACHE_H
#define HTTP_CPP_PROXY_MAP_CACHE_H

#include <map>
#include <cassert>

#include "cache.h"

namespace aiwannafly {
    template <class T>
    class MapCache final : public Cache<T> {
    public:

        MapCache() {
            this->table = new std::map<std::string, T*>();
            rwlock = new pthread_rwlock_t;
            int code = pthread_rwlock_init(rwlock, nullptr);
            assert(code == 0);
        }

        ~MapCache() {
            delete table;
            pthread_rwlock_destroy(rwlock);
            delete rwlock;
        }

        T *get(const std::string& key) override {
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

        bool put(const std::string & key, T *value) override {
            pthread_rwlock_wrlock(rwlock);
            (*table)[key] = value;
            pthread_rwlock_unlock(rwlock);
            return true;
        };

        size_t size() override {
            return table->size();
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
        std::map<std::string, T*> *table;
        pthread_rwlock_t *rwlock;
    };
}

#endif //HTTP_CPP_PROXY_MAP_CACHE_H
