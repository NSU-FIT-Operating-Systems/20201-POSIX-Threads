#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <pthread.h>

#include "copy_info.h"

struct node {
    struct copy_info *info;
    struct node *next;
};

struct stack {
    pthread_mutex_t mutex;
    struct node *head;
	atomic_size_t traversing_dir_cnt;
    // Once stop detected, a stack won't start again
    bool stopped;
};

void init_stack(struct stack *stack);

bool stack_stopped(struct stack *stack);

bool stack_push(struct stack *stack, struct copy_info *info);

// Returns last inserted copy_info. Waits untill inserted.
// Returns NULL only if stopped.
struct copy_info *stack_pop(struct stack *stack);

void free_stack(struct stack *stack);

void stack_register_dir(struct stack *stack);

void stack_unregister_dir(struct stack *stack);