#ifndef COPY_TASK_H
#define COPY_TASK_H

#include <stddef.h>

#include "paths_pair.h"

enum TASK_TYPE {
	DIRECTORY,
	REGULAR_FILE,
};

typedef struct copy_task {
	int type;
	paths_pair* paths;
} copy_task;

typedef struct tasks_list_node {
	copy_task task;
	struct tasks_list_node* next;
} tasks_list_node;

typedef struct paths_with_children {
	paths_pair* paths;
	tasks_list_node* children_list_head;
} paths_with_children;

copy_task create_task(enum TASK_TYPE type, paths_pair* paths);

int get_children_for_task(copy_task task, size_t* children_count, 
						tasks_list_node** tasks_for_direct_children);

void free_tasks_list(tasks_list_node* head);

#endif
