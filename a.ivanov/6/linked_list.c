#include "linked_list.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct list_node_t {
    struct list_node_t *next;
    struct list_node_t *prev;
    void *value;
} list_node_t;

struct list_t {
    list_node_t *head;
    list_node_t *tail;
    size_t len;
};

static list_node_t *get_by_pos(list_t *list, int pos) {
    assert(list);
    assert(pos >= 0 && pos < list->len);
    list_node_t *current = list->head;
    int idx = 0;
    while (idx < pos) {
        current = current->next;
        idx++;
    }
    return current;
}

static void push_to_tail_node(list_t *list, list_node_t *node) {
    assert(list);
    assert(node);
    assert(list->len != 0);
    if (1 == list->len) {
        // nothing to swap
        return;
    }
    assert(node->next || node->prev); //we need only nodes from the list_t
    if (NULL == node->next) {
        // then it's already in the tail, nothing to do
        return;
    }
    node->next->prev = node->prev;
    if (NULL == node->prev) {
        list->head = list->head->next;
        list->head->prev = NULL;
    } else {
        node->prev->next = node->next;
    }
    list->tail->next = node;
    node->next = NULL;
    node->prev = list->tail;
    list->tail = node;
}

void *show(list_t *list) {
    assert(list);
    if (list->len == 0) {
        return NULL;
    }
    return list->tail->value;
}

size_t size(list_t *list) {
    assert(list);
    return list->len;
}

void push_to_tail(list_t *list, int idx) {
    push_to_tail_node(list, get_by_pos(list, idx));
}

static list_node_t *pop_list_node(list_t *list) {
    assert(list);
    if (0 == list->len) {
        return NULL;
    }
    list_node_t *head = list->head;
    if (1 == list->len) {
        list->head = NULL;
        list->tail = NULL;
        list->len--;
        return head;
    }
    list->head = head->next;
    list->head->prev = NULL;
    list->len--;
    return head;
}

void iter(list_t *list, void (f)(void *)) {
    list_node_t *current = list->head;
    while (current != NULL) {
        f(current->value);
        current = current->next;
    }
}

void map(list_t *list, void *(f)(void *)) {
    list_node_t *current = list->head;
    while (current != NULL) {
        current->value = f(current->value);
        current = current->next;
    }
}

static bool append_list_node(list_t *list, list_node_t *new_node) {
    assert(list);
    if (NULL == new_node) {
        return false;
    }
    if (0 == list->len) {
        list->head = new_node;
        list->tail = new_node;
        list->len++;
        return true;
    }
    if (1 == list->len) {
        list->tail = new_node;
        list->head->next = list->tail;
        list->head->prev = NULL;
        list->tail->prev = list->head;
        list->tail->next = NULL;
        list->len++;
        return true;
    }
    list_node_t *prevTail = list->tail;
    prevTail->next = new_node;
    list->tail = new_node;
    list->tail->next = NULL;
    list->tail->prev = prevTail;
    list->len++;
    return true;
}

static list_node_t *init_list_node(const void *value) {
    assert(value);
    list_node_t *initial_node = (list_node_t*) calloc(1, sizeof(*initial_node));
    if (NULL == initial_node) {
        return NULL;
    }
    initial_node->value = (void*) value;
    return initial_node;
}

bool append(list_t *list, const void *value) {
    assert(list);
    assert(value);
    list_node_t *list_node = init_list_node(value);
    if (NULL == list_node) {
        return false;
    }
    return append_list_node(list, list_node);
}

void *pop(list_t *list) {
    assert(list);
    list_node_t *list_node = pop_list_node(list);
    if (NULL == list_node) {
        return NULL;
    }
    void *value = list_node->value;
    free(list_node);
    return value;
}

list_t *init_list() {
    list_t *initial_list = (list_t *) calloc(1, sizeof(*initial_list));
    return initial_list;
}

void print_list(FILE *fp_output, const list_t *list, void (*print_list_node_value)(FILE *, void *)) {
    assert(fp_output && list);
    list_node_t *current_node = list->head;
    while (NULL != current_node) {
        print_list_node_value(fp_output, current_node->value);
        current_node = current_node->next;
    }
    fprintf(fp_output, "\n");
}

void free_list(list_t *list, void (*free_value)(void *)) {
    if (NULL == list) {
        return;
    }
    list_node_t *current_node = list->head;
    while (NULL != current_node) {
        list_node_t *temp = current_node;
        current_node = current_node->next;
        if (free_value != NULL) {
            free_value(temp->value);
        }
        free(temp);
    }
    free(list);
}

list_t *insert_sub_list(list_t *main_list, list_t *sub_list, int pos) {
    assert(main_list && sub_list);
    assert(pos > 0 && pos < main_list->len);
    if (main_list->len == 1) {
        return sub_list;
    }
    list_node_t *insert_pos = main_list->head;
    int idx = 0;
    while (idx < pos) {
        insert_pos = insert_pos->next;
        idx++;
    }
    if (insert_pos->prev != NULL) {
        insert_pos->prev->next = sub_list->head;
        sub_list->head->prev = insert_pos->prev;
    }
    if (insert_pos->next != NULL) {
        insert_pos->next->prev = sub_list->tail;
        sub_list->tail->next = insert_pos->next;
    }
    if (insert_pos->prev == NULL) {
        main_list->head = sub_list->head;
    }
    if (insert_pos->next == NULL) {
        main_list->tail = sub_list->tail;
    }
    free(insert_pos);
    return main_list;
}
