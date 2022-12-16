#ifndef HTTP_CPP_PROXY_MAP_CACHE_H
#define HTTP_CPP_PROXY_MAP_CACHE_H

#include <map>

#include "cache.h"

namespace aiwannafly {
    template <class T>
    class map_cache final : public cache<T> {
    public:

        map_cache() {
            this->table = new std::map<std::string, T*>();
        }

        ~map_cache() {
            delete table;
        }

        T *get(const std::string& key) {
            return table->at(key);
        }

        void erase(const std::string &key) {
            table->erase(key);
        }

        bool contains(const std::string &key) {
            return table->contains(key);
        }

        bool put(const std::string & key, T *value) {
            (*table)[key] = value;
            return true;
        };

        size_t size() {
            return table->size();
        }

        size_t size_bytes(std::function<size_t(T *)> size) {
            size_t total_size = 0;
            for (auto it = table->begin(); it != table->end(); it++) {
                total_size += size(it->second);
            }
            return total_size;
        }

        void clear() {
            delete table;
            this->table = new std::map<std::string, T*>();
        }

    private:
        std::map<std::string, T*> *table;
    };
}


#endif //HTTP_CPP_PROXY_MAP_CACHE_H
