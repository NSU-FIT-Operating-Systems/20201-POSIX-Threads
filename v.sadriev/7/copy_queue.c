#include "copy_queue.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

queue_t *create_queue() {
	queue_t *queue = (queue_t *)malloc(sizeof(*queue));
	if (queue == NULL) {
		return NULL;
	}
	queue->head = NULL;
	queue->tail = NULL;
	queue->size = 0;
	pthread_mutex_init(&queue->mutex, NULL);
	return queue;
}

static queue_node_t *create_node(copy_task_t *task) {
	assert(task != NULL);
	queue_node_t *node = (queue_node_t *)malloc(sizeof(*node));
	if (node == NULL) {
		return NULL;
	}
	node->next = NULL;
	node->task = task;
	return node;
}

bool push(queue_t *queue, copy_task_t *task) {
	assert(queue != NULL);
	assert(task != NULL);
	queue_node_t *new_node = create_node(task);
	if (new_node == NULL) {
		return true;
	}

	pthread_mutex_lock(&queue->mutex);
	if (queue->head == NULL) {
		queue->head = new_node;
		queue->tail = new_node;
	} else {
		queue->tail->next = new_node;
		queue->tail = new_node;
	}
	++queue->size;
	pthread_mutex_unlock(&queue->mutex);
	return false;
}

copy_task_t *pop(queue_t *queue) {
	assert(queue != NULL);
	queue_node_t *res_node;
	pthread_mutex_lock(&queue->mutex);
	if (queue->head == NULL) {
		res_node = NULL;
	} else {
		res_node = queue->head;
		queue->head = queue->head->next;
	}
	--queue->size;
	pthread_mutex_unlock(&queue->mutex);
	if (res_node == NULL) {
		return NULL;
	}
	copy_task_t *task = res_node->task;
	free(res_node);
	return task;
}

static void free_node(queue_node_t *node) {
	if (node != NULL) {
		free_task(node->task);
		free(node);
	}
}

void free_queue(queue_t *queue) {
	if (queue == NULL) {
		return;
	}
	while(queue->head != NULL) {
		queue_node_t *tmp = queue->head;
		queue->head = tmp->next;
		free_node(tmp);
	}
	pthread_mutex_destroy(&queue->mutex);
	free(queue);
}

bool is_empty(queue_t *queue) {
	assert(queue != NULL);
	size_t size;
	pthread_mutex_lock(&queue->mutex);
	size = queue->size;
	pthread_mutex_unlock(&queue->mutex);
//	printf("q size: %zu\n", size);
	return size == 0;
}
