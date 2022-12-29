#ifndef LAB7__COPY_QUEUE_H_
#define LAB7__COPY_QUEUE_H_

#include <stddef.h>
#include <pthread.h>
#include <stdlib.h>

#include <stdbool.h>

#include "copy_task.h"

typedef struct Q_node {
	copy_task_t *task;
	struct Q_node *next;
} queue_node_t;

typedef struct Queue {
	queue_node_t *head;
	queue_node_t *tail;
	size_t size;
	pthread_mutex_t mutex;
} queue_t;

queue_t *create_queue();
bool push(queue_t *queue, copy_task_t *task);
copy_task_t *pop(queue_t *queue);
void free_queue(queue_t *queue);
bool is_empty(queue_t *queue);

#endif //LAB7__COPY_QUEUE_H_
