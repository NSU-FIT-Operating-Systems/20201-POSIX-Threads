#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>

#include "open_with_retry.h"
#include "copy_task.h"
#include "append_name_to_path.h"

static int count_and_create_tasks_list_for_dir_children(
								paths_pair* paths, 
								size_t* children_count,
								tasks_list_node** tasks_for_direct_children);

copy_task create_task(enum TASK_TYPE type, paths_pair* paths) {
	copy_task result = {type, paths};
	return result;
}

int get_children_for_task(copy_task task, size_t* children_count, 
						tasks_list_node** tasks_for_direct_children) {
	assert(NULL != tasks_for_direct_children);
	if (REGULAR_FILE == task.type) {
		*children_count = 0;
		*tasks_for_direct_children = NULL;
		return 0;
	}
	return count_and_create_tasks_list_for_dir_children(
												task.paths,
												children_count,
												tasks_for_direct_children);
}

void free_tasks_list(tasks_list_node* head) /*Not paths; they are in queue*/ {
	if (NULL == head) {
		return;
	}
	free_tasks_list(head->next);
	free(head);
}

static int count_and_create_tasks_list_for_dir_children(
								paths_pair* paths,
								size_t* children_count,
								tasks_list_node** tasks_for_direct_children) {
	assert(NULL != paths);
	assert(NULL != children_count);
	assert(NULL != tasks_for_direct_children);
	size_t waiting_period_nanoseconds = 50000000;
	int directory_fd = open_with_retry(waiting_period_nanoseconds, paths->src_path, O_RDONLY, 0);
	DIR* dir = fdopendir(directory_fd);
	if (NULL == dir) {
		return -1;
	}
	struct dirent* dir_entry = NULL;
	size_t children = 0;
	tasks_list_node* children_tasks = NULL;
	tasks_list_node* children_tasks_tail = NULL;
	long return_status = 0;
	while (NULL != (dir_entry = readdir(dir))) {
		if ((!strcmp(dir_entry->d_name, ".")) || 
			(!strcmp(dir_entry->d_name, ".."))) {
			continue;
		}
		char* cur_path = append_name_to_path(paths->src_path, dir_entry->d_name);
		if (NULL == cur_path) {
			return_status = -1;
			break;
		}
		struct stat stat;
		if (lstat(cur_path, &stat)) {
			free(cur_path);
			return_status = -1;
			break;
		}
		if (!(S_ISREG(stat.st_mode) || S_ISDIR(stat.st_mode))) {
			free(cur_path);
			continue;
		}
		paths_pair* next_paths = malloc(sizeof(*next_paths));
		if (NULL == next_paths) {
			free(cur_path);
			return_status = -1;
			break;
		}
		char* cur_new_path = append_name_to_path(paths->dst_path, dir_entry->d_name);
		if (NULL == cur_new_path) {
			free(cur_path);
			free(next_paths);
			return_status = -1;
			break;
		}
		next_paths->src_path = cur_path;
		next_paths->dst_path = cur_new_path;
		tasks_list_node* new_list_node = malloc(sizeof(*new_list_node));
		if (NULL == new_list_node) {
			free_paths_pair(next_paths);
			return_status = -1;
			break;
		}
		copy_task new_task = {S_ISDIR(stat.st_mode) ? DIRECTORY : REGULAR_FILE, next_paths};
		new_list_node->task = new_task;
		new_list_node->next = NULL;
		if (NULL == children_tasks) {
			children_tasks = new_list_node;
			children_tasks_tail = new_list_node;
		}
		else {
			children_tasks_tail->next = new_list_node;
			children_tasks_tail = new_list_node;
		}
		children++;
	}
	if (0 > return_status) {
		free_tasks_list(children_tasks);
	}
	*children_count = children;
	*tasks_for_direct_children = children_tasks;
	closedir(dir);
	return return_status;
}
