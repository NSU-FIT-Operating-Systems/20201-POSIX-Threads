#ifndef COPY_TASKS_QUEUE_H
#define COPY_TASKS_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

typedef struct paths_pair {
	char* src_path;
	char* dst_path;
} paths_pair;

void free_paths_pair(paths_pair* pair);

enum TASK_TYPE {
	DIRECTORY,
	REGULAR_FILE,
};

typedef struct copy_task {
	int type;
	paths_pair* paths;
	int children;
} copy_task;

typedef struct copy_tasks_queue_node {
	copy_task value;
	struct copy_tasks_queue_node* next;
} copy_tasks_queue_node;

typedef struct copy_tasks_queue {
	copy_tasks_queue_node* head;
	size_t size;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	bool interrupted;
} copy_tasks_queue;

copy_tasks_queue* new_queue();

int queue_push_task(copy_tasks_queue* queue, enum TASK_TYPE type, 
												paths_pair* paths);

int queue_pop(copy_tasks_queue* queue, copy_task* value);

void free_queue(copy_tasks_queue* queue);

void queue_interrupt_wait(copy_tasks_queue* queue);

#endif
