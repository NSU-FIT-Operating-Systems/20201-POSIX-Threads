#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct list_t list_t;

list_t *init_list();

size_t size(list_t *list);

/*
 * Adds new list_node_t with the value to the tail of
 * the list_t.
 */
bool append(list_t *list, const void *value);

/*
 * Cuts off the head of the list_t and returns it's value.
 * So the size of a list_t decrements.
 */
void *pop(list_t *list);

/*
 * returns value of a tail, but doesn't cut it
 */
void *show(list_t *list);

void iter(list_t *list, void (f)(void *));

void map(list_t *list, void *(f)(void *));

/*
 * Insertion position is a node from the main list, which should be freed
 * and replaced with nodes of the sublist.
 */
list_t *insert_sub_list(list_t *main_list, list_t *sub_list, int pos);

void push_to_tail(list_t *list, int idx);

void print_list(FILE *fp_output, const list_t *list, void (*print_list_node_value)(FILE *, void *));

void free_list(list_t *list, void (*free_value)(void *));

#endif //LINKED_LIST_H
