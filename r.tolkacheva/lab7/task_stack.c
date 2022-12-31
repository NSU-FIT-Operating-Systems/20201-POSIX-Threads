#include "task_stack.h"

#include <stdio.h>
#include <stdlib.h>

#include "sleep.h"

void init_stack(struct stack *stack) {
    pthread_mutex_init(&stack->mutex, NULL);
    stack->head = NULL;
    atomic_store(&stack->traversing_dir_cnt, 0);
    stack->stopped = false;
}

static bool stack_stopped_internal(struct stack *stack) {
    if (stack->stopped) {
        return true;
    }
    return (stack->head == NULL) && (atomic_load(&stack->traversing_dir_cnt) == 0);
}

bool stack_stopped(struct stack *stack) {
    bool retval = false;

    // Lock the mutex
    pthread_mutex_lock(&stack->mutex);

    if (stack->stopped) {
        // Unlock the mutex
        pthread_mutex_unlock(&stack->mutex);

        return true;
    }

    retval = (stack->head == NULL) && (atomic_load(&stack->traversing_dir_cnt) == 0);
    if (retval) {
        stack->stopped = true;
    }

    // Unlock the mutex
    pthread_mutex_unlock(&stack->mutex);

    if (retval) {
        stack->stopped = true;
    }

    return retval;
}

void stack_register_dir(struct stack *stack) {
    atomic_fetch_add(&stack->traversing_dir_cnt, 1);
}

void stack_unregister_dir(struct stack *stack) {
    atomic_fetch_sub(&stack->traversing_dir_cnt, 1);
}

bool stack_push(struct stack *stack, struct copy_info *info) {
    // Allocate memory for the new node
    struct node *node = (struct node *)malloc(sizeof(struct node));
    if (node == NULL) {
        fprintf(stderr, "Error: Unable to allocate memory\n");
        return false;
    }

    // Initialize the new node
    node->info = info;
    node->next = NULL;

    // Lock the mutex
    pthread_mutex_lock(&stack->mutex);

    // Push the new node onto the stack
    node->next = stack->head;
    stack->head = node;

    // Unlock the mutex
    pthread_mutex_unlock(&stack->mutex);

    return true;
}

struct copy_info *stack_pop(struct stack *stack) {
    // Lock the mutex
    pthread_mutex_lock(&stack->mutex);

    while (stack->head == NULL) {
        if (stack_stopped_internal(stack)) {
            // Unlock the mutex
            pthread_mutex_unlock(&stack->mutex);
            return NULL;
        }
        // Unlock the mutex
        pthread_mutex_unlock(&stack->mutex);

        my_sleep();

        // Lock the mutex
        pthread_mutex_lock(&stack->mutex);
    }

    // Pop the top node from the stack
    struct node *node = stack->head;
    stack->head = node->next;

    // Unlock the mutex
    pthread_mutex_unlock(&stack->mutex);

    // Get the copy_info struct from the popped node
    struct copy_info *info = node->info;

    // Free the memory for the popped node
    free(node);

    return info;
}

void free_stack(struct stack *stack) {
    // Lock the mutex
    pthread_mutex_lock(&stack->mutex);

    // Free the memory for all nodes in the stack
    struct node *node = stack->head;
    while (node != NULL) {
        struct node *next = node->next;
        free(node->info->src_path);
        free(node->info->dst_path);
        free(node->info);
        free(node);
        node = next;
    }

    // Unlock the mutex
    pthread_mutex_unlock(&stack->mutex);

    // Destroy the mutex
    pthread_mutex_destroy(&stack->mutex);
}
