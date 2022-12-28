#ifndef HTTP_CPP_PROXY_MAP_CACHE_H
#define HTTP_CPP_PROXY_MAP_CACHE_H

#include <map>
#include <cassert>
#include <pthread.h>

#include "cache.h"

namespace aiwannafly {
    template <class T>
    class MapCache final : public Cache<T> {
    public:

        MapCache() {
            this->table = new std::map<std::string, T*>();
            mutex = new pthread_mutex_t;
            int code = pthread_mutex_init(mutex, nullptr);
            assert(code == 0);
        }

        ~MapCache() {
            delete table;
            pthread_mutex_destroy(mutex);
            delete mutex;
        }

        T *get(const std::string& key) override {
            pthread_mutex_lock(mutex);
            auto *res = table->at(key);
            pthread_mutex_unlock(mutex);
            return res;
        }

        void erase(const std::string &key) override {
            pthread_mutex_lock(mutex);
            table->erase(key);
            pthread_mutex_unlock(mutex);
        }

        bool contains(const std::string &key) override {
            pthread_mutex_lock(mutex);
            bool res = table->contains(key);
            pthread_mutex_unlock(mutex);
            return res;
        }

        bool put(const std::string & key, T *value) override {
            pthread_mutex_lock(mutex);
            (*table)[key] = value;
            pthread_mutex_unlock(mutex);
            return true;
        };

        size_t size() override {
            return table->size();
        }

        size_t sizeBytes(std::function<size_t(const T *)> size) override {
            pthread_mutex_lock(mutex);
            size_t total_size = 0;
            for (auto it = table->begin(); it != table->end(); it++) {
                total_size += size(it->second);
            }
            pthread_mutex_unlock(mutex);
            return total_size;
        }

        void clear() override {
            pthread_mutex_lock(mutex);
            for (auto it = table->begin(); it != table->end(); it++) {
                delete it->second;
            }
            table->clear();
            pthread_mutex_unlock(mutex);
        }

    private:
        std::map<std::string, T*> *table;
        pthread_mutex_t *mutex;
    };
}

#endif //HTTP_CPP_PROXY_MAP_CACHE_H
