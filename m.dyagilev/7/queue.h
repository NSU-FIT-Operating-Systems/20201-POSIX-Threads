#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include <stdatomic.h>

#include "queue_value.h"

typedef struct queue_node {
	queue_value value;
	struct queue_node* next;
} queue_node;

typedef struct queue {
	queue_node* head;
	queue_node* tail;
	atomic_size_t size;
	pthread_mutex_t mutex;
	bool interrupted;
} queue;

queue* new_queue();

int queue_push(queue* queue, queue_value value);

int queue_pop(queue* queue, queue_value* value);

void free_queue(queue* queue);

void queue_interrupt_wait(queue* queue);

#endif
