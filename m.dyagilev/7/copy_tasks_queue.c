#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "copy_tasks_queue.h"
#include "opendir_with_retry.h"

void free_paths_pair(paths_pair* pair) {
	free(pair->src_path);
	free(pair->dst_path);
	free(pair);
}

static int count_dir_children(char* path);
static int queue_push(copy_tasks_queue* queue, copy_task value);

copy_tasks_queue* new_queue() {
	copy_tasks_queue* result = malloc(sizeof(*result));
	if (NULL == result) {
		return NULL;
	}
	result->head = NULL;
	result->size = 0;
	pthread_mutex_init(&(result->mutex), NULL);
	pthread_cond_init(&(result->cond), NULL);
	result->interrupted = false;
	return result;
}

int queue_push_task(copy_tasks_queue* queue, enum TASK_TYPE type, 
												paths_pair* paths) {
	if (NULL == queue) {
		return 0;
	}
	assert(NULL != paths);
	copy_task task;
	task.type = type;
	task.paths = paths;
	if (DIRECTORY == type) {
		task.children = count_dir_children(paths->src_path);
	}
	else {
		task.children = 0;
	}
	return queue_push(queue, task);
}

int queue_pop(copy_tasks_queue* queue, copy_task* value) {
	assert(NULL != queue);
	assert(NULL != value);
	pthread_mutex_lock(&(queue->mutex));
	while ((0 == queue->size) && !queue->interrupted) {
		pthread_cond_wait(&(queue->cond), &(queue->mutex));
	}
	if (queue->interrupted) {
		pthread_mutex_unlock(&(queue->mutex));
		return -1;
	}
	*value = queue->head->value;
	copy_tasks_queue_node* new_head = queue->head->next;
	free(queue->head);
	queue->head = new_head;
	queue->size--;
	pthread_mutex_unlock(&(queue->mutex));
	return 0;
}

void free_queue(copy_tasks_queue* queue) {
	if (NULL == queue) {
		return;
	}
	copy_tasks_queue_node* cur_node = queue->head;
	while (NULL != cur_node) {
		copy_tasks_queue_node* next_node = cur_node->next;
		free(cur_node);
		cur_node = next_node;
	}
	pthread_mutex_destroy(&(queue->mutex));
	pthread_cond_destroy(&(queue->cond));
	free(queue);
}

void queue_interrupt_wait(copy_tasks_queue* queue) {
	assert(NULL != queue);
	pthread_mutex_lock(&(queue->mutex));
	queue->interrupted = true;
	pthread_cond_signal(&(queue->cond));
	pthread_mutex_unlock(&(queue->mutex));
}

static int count_dir_children(char* path) {
	assert(NULL != path);
	size_t waiting_period_nanoseconds = 50000000;
	DIR* dir = opendir_with_retry(waiting_period_nanoseconds, path);
	if (NULL == dir) {
		return -1;
	}
	struct dirent* dir_entry = NULL;
	size_t result = 0;
	while (NULL != (dir_entry = readdir(dir))) {
		struct stat stat;
		if (fstatat(dirfd(dir), dir_entry->d_name, &stat, AT_SYMLINK_NOFOLLOW)) {
			result = -1;
			break;
		}
		if (S_ISREG(stat.st_mode) || S_ISDIR(stat.st_mode)) {
			result++;
		}
	}
	closedir(dir);
	return result - 2; //There always are '.' and '..'
}

static int queue_push(copy_tasks_queue* queue, copy_task value) {
	assert(NULL != queue);
	copy_tasks_queue_node* new_node = malloc(sizeof(*new_node));
	if (NULL == new_node) {
		return -1;
	}
	new_node->value = value;
	new_node->next = NULL;
	pthread_mutex_lock(&(queue->mutex));
	if (NULL == queue->head) {
		queue->head = new_node;
	}
	else {
		copy_tasks_queue_node* cur_node = queue->head;
		while (NULL != cur_node->next) {
			cur_node = cur_node->next;
		}
		cur_node->next = new_node;
	}
	queue->size++;
	pthread_cond_signal(&(queue->cond));
	pthread_mutex_unlock(&(queue->mutex));
	return 0;
}
