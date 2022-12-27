#include <stdlib.h>
#include <assert.h>

#include "thread_safe_list.h"

struct ts_list_t {
    list_t *simple_list;
    pthread_rwlock_t *rwlock;
};

ts_list_t *init_ts_list() {
    ts_list_t *res = (ts_list_t *) malloc(sizeof(*res));
    if (res == NULL) {
        return NULL;
    }
    res->simple_list = init_list();
    if (res->simple_list == NULL) {
        free(res);
        return NULL;
    }
    res->rwlock = (pthread_rwlock_t *) malloc(sizeof(pthread_rwlock_t));
    if (res->rwlock == NULL) {
        free(res->simple_list);
        free(res);
        return NULL;
    }
    int return_code = pthread_rwlock_init(res->rwlock, NULL);
    if (return_code != 0) {
        free(res->simple_list);
        free(res->rwlock);
        free(res);
        return NULL;
    }
    return res;
}

static void assert_thread_safe_list(const ts_list_t *tlist) {
    assert(tlist);
    assert(tlist->rwlock);
    assert(tlist->simple_list);
}

void *show_ts(ts_list_t *tlist) {
    assert_thread_safe_list(tlist);
    void *res;
    pthread_rwlock_rdlock(tlist->rwlock);
    res = show(tlist->simple_list);
    pthread_rwlock_unlock(tlist->rwlock);
    return res;
}

bool append_ts(ts_list_t *tlist, const void *value) {
    assert_thread_safe_list(tlist);
    bool res = false;
    pthread_rwlock_wrlock(tlist->rwlock);
    res = append(tlist->simple_list, value);
    pthread_rwlock_unlock(tlist->rwlock);
    return res;
}

void *pop_ts(ts_list_t *tlist) {
    assert_thread_safe_list(tlist);
    void *res = NULL;
    pthread_rwlock_wrlock(tlist->rwlock);
    res = pop(tlist->simple_list);
    pthread_rwlock_unlock(tlist->rwlock);
    return res;
}

void iter_ts(ts_list_t *tlist, void (f)(void *)) {
    assert_thread_safe_list(tlist);
    pthread_rwlock_rdlock(tlist->rwlock);
    iter(tlist->simple_list, f);
    pthread_rwlock_unlock(tlist->rwlock);
}

void map_ts(ts_list_t *tlist, void *(f)(void *)) {
    assert_thread_safe_list(tlist);
    pthread_rwlock_wrlock(tlist->rwlock);
    map(tlist->simple_list, f);
    pthread_rwlock_unlock(tlist->rwlock);
}

void print_ts_list(FILE *fp_output, const ts_list_t *tlist,
                   void (*print_list_node_value)(FILE *, void *)) {
    assert_thread_safe_list(tlist);
    assert(fp_output);
    pthread_rwlock_rdlock(tlist->rwlock);
    print_list(fp_output, tlist->simple_list, print_list_node_value);
    pthread_rwlock_unlock(tlist->rwlock);
}

void free_ts_list(ts_list_t *list, void (*free_value)(void *)) {
    assert_thread_safe_list(list);
    pthread_rwlock_wrlock(list->rwlock);
    free_list(list->simple_list, free_value);
    pthread_rwlock_unlock(list->rwlock);
    pthread_rwlock_destroy(list->rwlock);
    free(list->rwlock);
    free(list);
}
