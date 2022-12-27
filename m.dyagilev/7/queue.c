#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <time.h>

#include "queue.h"

#define SLEEP_DELAY_NANOSECONDS 500000000

queue* new_queue() {
	queue* result = malloc(sizeof(*result));
	if (NULL == result) {
		return NULL;
	}
	result->head = NULL;
	result->tail = NULL;
	result->size = 0;
	pthread_mutex_init(&(result->mutex), NULL);
	result->interrupted = false;
	return result;
}

int queue_push(queue* queue, queue_value value) {
	assert(NULL != queue);
	queue_node* new_node = malloc(sizeof(*new_node));
	if (NULL == new_node) {
		return -1;
	}
	new_node->value = value;
	new_node->next = NULL;
	pthread_mutex_lock(&(queue->mutex));
	if (NULL != queue->tail) {
		//At each moment head and tail are either both NULL or both not NULL
		assert(NULL != queue->head); //Or we have a critical mistake somewhere
		queue->tail->next = new_node;
		queue->tail = new_node;
	}
	else {
		assert(NULL == queue->head); 
		queue->head = new_node;
		queue->tail = new_node;
	}
	pthread_mutex_unlock(&(queue->mutex));
	atomic_fetch_add(&(queue->size), 1);
	return 0;
}


int queue_pop(queue* queue, queue_value* value) {
	assert(NULL != queue);
	assert(NULL != value);
	struct timespec delay = {SLEEP_DELAY_NANOSECONDS / 1000000000,
							SLEEP_DELAY_NANOSECONDS % 1000000000};
	while ((0 == queue->size) && !queue->interrupted) {
		nanosleep(&delay, NULL);
	}
	if (queue->interrupted) {
		return -1;
	}
	pthread_mutex_lock(&(queue->mutex));
	*value = queue->head->value;
	queue_node* old_head = queue->head;
	queue->head = old_head->next;
	if (NULL == queue->head) {
		queue->tail = NULL;
	}
	pthread_mutex_unlock(&(queue->mutex));
	free(old_head);
	atomic_fetch_sub(&(queue->size), 1);
	return 0;
}

void free_queue(queue* queue) {
	if (NULL == queue) {
		return;
	}
	queue_node* cur_node = queue->head;
	while (NULL != cur_node) {
		queue_node* next_node = cur_node->next;
		free(cur_node);
		cur_node = next_node;
	}
	pthread_mutex_destroy(&(queue->mutex));
	free(queue);
}

void queue_interrupt_wait(queue* queue) {
	assert(NULL != queue);
	queue->interrupted = true;
}
