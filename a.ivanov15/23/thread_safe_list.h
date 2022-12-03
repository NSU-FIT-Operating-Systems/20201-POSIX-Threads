#ifndef INC_6_THREAD_SAFE_LIST_H
#define INC_6_THREAD_SAFE_LIST_H

/*
 * aiwannafly/threads/6/thread_safe_list.h
 * Copyright (C) 2022 Alexander Ivanov
 */

#include "linked_list.h"

#include <pthread.h>

typedef struct ts_list_t ts_list_t;

ts_list_t *init_ts_list();

bool append_ts(ts_list_t *tlist, const void *value);

void *pop_ts(ts_list_t *tlist);

// returns value of a tail, but doesn't cut it
void *show_ts(ts_list_t *tlist);

void iter_ts(ts_list_t *tlist, void (f)(void *));

void map_ts(ts_list_t *tlist, void *(f)(void *));

void print_ts_list(FILE *fp_output, const ts_list_t *tlist,
                   void (*print_list_node_value)(FILE *, void *));

// you must be sure that at a moment of a call to the function
// no more threads proceed with operations over the list
void free_ts_list(ts_list_t *list, void (*free_value)(void *));

#endif //INC_6_THREAD_SAFE_LIST_H
