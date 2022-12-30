#include "copy_task.h"
#include <assert.h>
#include <stdlib.h>

 copy_task_t *create_task(enum TASK_TYPE type, paths_t *paths) {
	assert(paths != NULL);
	copy_task_t *task = (copy_task_t *)malloc(sizeof(*task));
	if (task == NULL) {
		return NULL;
	}
	task->type = type;
	task->paths = paths;
	return task;
}

void free_task(copy_task_t *task) {
	if (task != NULL) {
		free_paths(task->paths);
		free(task);
	}
}