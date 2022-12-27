#ifndef LAB7__COPY_TASK_H_
#define LAB7__COPY_TASK_H_

#include "paths.h"

enum TASK_TYPE {
	DIRECTORY,
	REGULAR_FILE
};

typedef struct Task {
	enum TASK_TYPE type;
	paths_t *paths;
} copy_task_t;

copy_task_t *create_task(enum TASK_TYPE type, paths_t *paths);
void free_task(copy_task_t *task);

#endif //LAB7__COPY_TASK_H_
