#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>

#include "open_with_retry.h"
#include "append_name_to_path.h"
#include "copy_task.h"
#include "queue.h"

enum ERRORS {
	MISSING_ARGUMENT = 1,
	MEMORY_ALLOCATION_ERROR,
	FDOPENDIR_ERROR,
	RIGHTS_ERROR,
	MKDIR_ERROR,
	STAT_ERROR,
	PTHREAD_CREATE_ERROR,
	OPEN_ERROR,
	CLOSE_ERROR,
	READ_ERROR,
	WRITE_ERROR,
	QUEUE_ERROR,
	DIR_READING_ERROR,
	ATTR_SETTING_ERROR,
	TASK_ERROR,
};

#define RESOURCES_WAITING_DELAY_NANOSECONDS 50000000
#define READ_BUFFER_SIZE 1024
#define ERR_MSG_BUF_SIZE 1024

queue* tasks_queue;
long error = 0;

void set_error_and_interrupt_queue_wait(long value) {
	error = value;
	queue_interrupt_wait(tasks_queue);
}

int close_with_errmsg(int fd) {
	int result = 0;
	if (0 != (result = close(fd))) {
		char buf[ERR_MSG_BUF_SIZE];
		strerror_r(errno, buf, ERR_MSG_BUF_SIZE);
		fprintf(stderr, "Warning: closing error, %s\n", buf);
	}
	return result;
}

int pthread_create_with_retry(size_t retry_period_nanoseconds,
								pthread_t* thread,
								pthread_attr_t* attr,
								void* (*start_routine)(void*),
								void* arg) {
	assert(NULL != thread);
	assert(NULL != start_routine);
	long result = 0;
	struct timespec delay = {retry_period_nanoseconds / 1000000000,
							retry_period_nanoseconds % 1000000000};
	while (true) {
		result = pthread_create(thread, attr, start_routine, arg);
		if (0 != result) {
			if (EAGAIN != result) {
				return result;
			}
			nanosleep(&delay, NULL);
		}
		else {
			break;
		}
	}
	return result;
}

void* copy_file(void* arg) {
	assert(NULL != arg);
	paths_pair* paths = (paths_pair*)arg;

	struct timespec delay = {RESOURCES_WAITING_DELAY_NANOSECONDS / 1000000000,
							RESOURCES_WAITING_DELAY_NANOSECONDS % 1000000000};

	int src_fd = 0;
	int dst_fd = 0;

	long opening_status = 0;
	while (true) {
		//Trying to open both files
		src_fd = open_with_retry(RESOURCES_WAITING_DELAY_NANOSECONDS,
									paths->src_path, O_RDONLY, 0);
		if (0 > src_fd) {
			opening_status = src_fd;
			break;
		}

		struct stat stat;
		if (fstat(src_fd, &stat)) {
			char buf[ERR_MSG_BUF_SIZE];
			strerror_r(errno, buf, ERR_MSG_BUF_SIZE);
			fprintf(stderr, "Stat error: %s, %s\n", paths->src_path, buf);
			close_with_errmsg(src_fd);
			opening_status = STAT_ERROR;
			break;
		}

		bool success = false;
		while (true) {
			dst_fd = open(paths->dst_path, O_WRONLY | O_CREAT, stat.st_mode);
			if (0 > dst_fd) {
				if ((EMFILE != errno) && (ENFILE != errno)) {
					char buf[ERR_MSG_BUF_SIZE];
					strerror_r(errno, buf, ERR_MSG_BUF_SIZE);
					fprintf(stderr, "Error while opening dst %s, %s\n", 
													paths->dst_path, buf);
					/*
					Next line is the reason why simple open_with_retry 
					is not suitable here
					*/
					close_with_errmsg(src_fd);
					opening_status = OPEN_ERROR;
				}
				else {
					opening_status = close_with_errmsg(src_fd);
					nanosleep(&delay, NULL);
				}
				break;
			}
			else {
				success = true;
				break;
			}
		}
		if ((0 == opening_status) && !success) {
			continue;
		}
		if ((0 != opening_status) || success) {
			break;
		}
	}

	if (0 != opening_status) {
		free_paths_pair(paths);
		set_error_and_interrupt_queue_wait(opening_status);
		pthread_exit((void*)opening_status);	
	}

	long return_status = 0;
	long reading_result = 0;
	void* buffer = malloc(READ_BUFFER_SIZE);
	if (NULL == buffer) {
		fprintf(stderr, "Not enough memory\n");
		close_with_errmsg(src_fd);
		close_with_errmsg(dst_fd);
		free_paths_pair(paths);
		set_error_and_interrupt_queue_wait(MEMORY_ALLOCATION_ERROR);
		pthread_exit((void*)MEMORY_ALLOCATION_ERROR);	
	}

	while (0 < (reading_result = read(src_fd, buffer, READ_BUFFER_SIZE))) {
		int writing_result = write(dst_fd, buffer, reading_result);
		if (reading_result != writing_result) {
			char buf[ERR_MSG_BUF_SIZE];
			strerror_r(errno, buf, ERR_MSG_BUF_SIZE);
			fprintf(stderr, "Writing error: %s, %s\n", paths->dst_path, buf);
			return_status = WRITE_ERROR;
			break;
		}
	}
	free(buffer);
	if (0 > reading_result) {
		char buf[ERR_MSG_BUF_SIZE];
		strerror_r(errno, buf, ERR_MSG_BUF_SIZE);
		fprintf(stderr, "Reading error: %s, %s\n", paths->src_path, buf);
		return_status = READ_ERROR;
	}
	free_paths_pair(paths);
	if (0 == return_status && 
			((0 != close_with_errmsg(src_fd)) || 
				(0 != close_with_errmsg(dst_fd)))) {
		return_status = CLOSE_ERROR;	
	}
	if (0 != return_status) {
		set_error_and_interrupt_queue_wait(return_status);
	}
	pthread_exit((void*)return_status);
}

void* copy_directory(void* arg) {
	assert(NULL != arg);
	paths_with_children* paths_and_children = (paths_with_children*)arg;
	paths_pair* paths = paths_and_children->paths;
	tasks_list_node* children_list = paths_and_children->children_list_head;
	int src_dir_fd = open_with_retry(RESOURCES_WAITING_DELAY_NANOSECONDS,
									paths->src_path, O_RDONLY, 0);
	if (0 > src_dir_fd) {
		fprintf(stderr, "Error while opening directory: %s\n", paths->src_path);
		free_paths_pair(paths);
		free_tasks_list(children_list);
		free(paths_and_children);
		set_error_and_interrupt_queue_wait(FDOPENDIR_ERROR);
		pthread_exit((void*)OPEN_ERROR);
	}

	DIR* src_dir = fdopendir(src_dir_fd);
	if (NULL == src_dir) {
		fprintf(stderr, "Error while opening directory stream: %s\n", paths->src_path);
		close_with_errmsg(src_dir_fd);
		free_paths_pair(paths);
		free_tasks_list(children_list);
		free(paths_and_children);
		set_error_and_interrupt_queue_wait(FDOPENDIR_ERROR);
		pthread_exit((void*)FDOPENDIR_ERROR);
	}

	if (0 != mkdir(paths->dst_path, 0700)) {
		if (EEXIST == errno) {
			if (access(paths->dst_path, W_OK | X_OK)) {
				fprintf(stderr, "Directory exists and access denied: %s\n", paths->dst_path);
				closedir(src_dir);
				free_paths_pair(paths);
				free_tasks_list(children_list);
				free(paths_and_children);
				set_error_and_interrupt_queue_wait(RIGHTS_ERROR);
				pthread_exit((void*)RIGHTS_ERROR);
			}
		} 
		else {
			fprintf(stderr, "Mkdir error: %s\n", paths->dst_path);
			closedir(src_dir);
			free_paths_pair(paths);
			free_tasks_list(children_list);
			free(paths_and_children);
			set_error_and_interrupt_queue_wait(MKDIR_ERROR);
			pthread_exit((void*)MKDIR_ERROR);
		}
	}

	tasks_list_node* cur_node = children_list;

	long return_status = 0;

	while (NULL != cur_node) {
		if (queue_push(tasks_queue, cur_node->task)) {
			return_status = QUEUE_ERROR;
			break;
		}
		cur_node = cur_node->next;
	}
	if (0 != return_status) {
		set_error_and_interrupt_queue_wait(return_status);
	}
	closedir(src_dir);
	free_paths_pair(paths);
	free_tasks_list(children_list);
	free(paths_and_children);
	pthread_exit((void*)return_status);
}

int main(int argc, char* argv[]) {
	if (3 > argc) {
		fprintf(stderr, "Need source and destination arguments\n");
		pthread_exit((void*)MISSING_ARGUMENT);
	}

	pthread_attr_t attr;
	if (0 != pthread_attr_init(&attr)) {
		return ATTR_SETTING_ERROR;
	}
	if (0 != pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) {
		return ATTR_SETTING_ERROR;
	}

	paths_pair* paths = malloc(sizeof(*paths));
	if (NULL == paths) {
		fprintf(stderr, "Not enough memory\n");
		free(paths);
		pthread_exit((void*)MEMORY_ALLOCATION_ERROR);
	}

	paths->src_path = strdup(argv[1]);
	paths->dst_path = strdup(argv[2]);

	if ((NULL == paths->src_path) || (NULL == paths->dst_path)) {
		fprintf(stderr, "Not enough memory\n");
		free_paths_pair(paths);
		pthread_exit((void*)MEMORY_ALLOCATION_ERROR);
	}

	tasks_queue = new_queue();
	if (NULL == tasks_queue) {
		fprintf(stderr, "Not enough memory\n");
		free_paths_pair(paths);
		pthread_exit((void*)MEMORY_ALLOCATION_ERROR);
	}

	copy_task first_task = create_task(DIRECTORY, paths);
	
	if (queue_push(tasks_queue, first_task)) {
		fprintf(stderr, "Queueing error\n");
		free_paths_pair(paths);
		free_queue(tasks_queue);
		pthread_exit((void*)QUEUE_ERROR);
	}

	size_t expected_tasks = 1;
	long return_status = 0;

	while ((0 < expected_tasks) && (0 == error)) {
		copy_task task;
		if (queue_pop(tasks_queue, &task)) {
			fprintf(stderr, "Queue error\n");
			return_status = QUEUE_ERROR;
			break;
		}
		if (0 != error) {
			return_status = error;
			break;
		}
		size_t task_children_num = 0;
		tasks_list_node* task_children;
		if (0 != get_children_for_task(task, &task_children_num,
										&task_children)) {
			fprintf(stderr, "Dir reading error\n");
			return_status = DIR_READING_ERROR;
			break;
		}
		expected_tasks--;
		expected_tasks += task_children_num;
		switch (task.type) {
			case DIRECTORY: {
				paths_with_children* paths_and_children = malloc(sizeof(*paths_and_children));
				if (NULL == paths_and_children) {
					fprintf(stderr, "Not enough memory\n");
					free_paths_pair(task.paths);
					free_tasks_list(task_children);
					return_status = MEMORY_ALLOCATION_ERROR;
					break;
				}
				paths_and_children->paths = task.paths;
				paths_and_children->children_list_head = task_children;
				pthread_t thread;
				int creation_result = 
					pthread_create_with_retry(RESOURCES_WAITING_DELAY_NANOSECONDS,
												&thread, &attr, copy_directory, paths_and_children);
				if (0 != creation_result) {
					fprintf(stderr, "Can't create thread for %s\n", task.paths->src_path);
					free(paths_and_children);
					free_paths_pair(task.paths);
					free_tasks_list(task_children);
					return_status = PTHREAD_CREATE_ERROR;
				}
			}
			break;
			case REGULAR_FILE: {
				pthread_t thread;
				int creation_result = 
					pthread_create_with_retry(RESOURCES_WAITING_DELAY_NANOSECONDS,
												&thread, &attr, copy_file, task.paths);
				if (0 != creation_result) {
					fprintf(stderr, "Can't create thread for %s\n", task.paths->src_path);
					free_paths_pair(task.paths);
					return_status = PTHREAD_CREATE_ERROR;
				}
			}
			break;
		}
		if (0 != return_status) {
			break;
		}
	}
	pthread_attr_destroy(&attr);
	free_queue(tasks_queue);
	pthread_exit((void*)return_status);
}
