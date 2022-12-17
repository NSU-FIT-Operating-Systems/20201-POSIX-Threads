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

#include "opendir_with_retry.h"
#include "copy_tasks_queue.h"

enum ERRORS {
	MISSING_ARGUMENT = 1,
	MEMORY_ALLOCATION_ERROR,
	OPENDIR_ERROR,
	RIGHTS_ERROR,
	MKDIR_ERROR,
	STAT_ERROR,
	PTHREAD_CREATE_ERROR,
	OPEN_ERROR,
	CLOSE_ERROR,
	READ_ERROR,
	WRITE_ERROR,
	QUEUE_ERROR,
	TASK_ERROR,
};

#define RESOURCES_WAITING_DELAY_NANOSECONDS 50000000
#define READ_BUFFER_SIZE 1024
#define ERR_MSG_BUF_SIZE 1024

copy_tasks_queue* queue;
pthread_mutex_t error_mutex;
long error = 0;

void set_error(long value) {
	pthread_mutex_lock(&error_mutex);
	error = value;
	pthread_mutex_unlock(&error_mutex);
}

long get_error() {
	pthread_mutex_lock(&error_mutex);
	long result = error;
	pthread_mutex_unlock(&error_mutex);
	return result;
}

void set_error_and_interrupt_queue_wait(long value) {
	set_error(value);
	queue_interrupt_wait(queue);
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

char* append_name_to_path(const char* path, const char* name) {
	assert(NULL != path);
	assert(NULL != name);
	size_t path_len = strlen(path);
	size_t name_len = strlen(name);
	char* new_path = malloc(path_len + (('/' == path[path_len - 1]) ? 0 : 1) + 
							name_len + 1 /* '\0' */);
	if (NULL == new_path) {
		return NULL;		
	}
	strcpy(new_path, path);
	if ('/' != new_path[strlen(path) - 1]) {
		strcat(new_path, "/");
	}
	return strcat(new_path, name);
}

int pthread_create_detached_with_retry(size_t retry_period_nanoseconds,
										pthread_t* thread,
										void* (*start_routine)(void*),
										void* arg) {
	assert(NULL != thread);
	assert(NULL != start_routine);
	pthread_attr_t attr;
	long result = 0;
	if (0 != (result = pthread_attr_init(&attr))) {
		return result;
	}
	if (0 != (result = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))) {
		return result;
	}
	struct timespec delay = {retry_period_nanoseconds / 1000000000,
							retry_period_nanoseconds % 1000000000};
	while (true) {
		result = pthread_create(thread, &attr, start_routine, arg);
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
	pthread_attr_destroy(&attr);
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
		while (true) {
			src_fd = open(paths->src_path, O_RDONLY);
			if (0 > src_fd) {
				if ((EMFILE != errno) && (ENFILE != errno)) {
					char buf[ERR_MSG_BUF_SIZE];
					strerror_r(errno, buf, ERR_MSG_BUF_SIZE);
					fprintf(stderr, "Error while opening src %s, %s\n", paths->src_path, buf);
					opening_status = OPEN_ERROR;
					break;
				}
				nanosleep(&delay, NULL);
			}
			else {
				break;
			}
		}
		if (0 != opening_status) {
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
					fprintf(stderr, "Error while opening dst %s, %s\n", paths->dst_path, buf);
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
	paths_pair* paths = (paths_pair*)arg;
	DIR* src_dir = opendir_with_retry(RESOURCES_WAITING_DELAY_NANOSECONDS,
										paths->src_path);
	if (NULL == src_dir) {
		fprintf(stderr, "Error while opening directory: %s\n", paths->src_path);
		free_paths_pair(paths);
		set_error_and_interrupt_queue_wait(OPENDIR_ERROR);
		pthread_exit((void*)OPENDIR_ERROR);
	}

	if (0 != mkdir(paths->dst_path, 0700)) {
		if (EEXIST == errno) {
			if (access(paths->dst_path, W_OK | X_OK)) {
				fprintf(stderr, "Directory exists and access denied: %s\n", paths->dst_path);
				closedir(src_dir);
				set_error_and_interrupt_queue_wait(RIGHTS_ERROR);
				pthread_exit((void*)RIGHTS_ERROR);
			}
		} 
		else {
			fprintf(stderr, "Mkdir error: %s\n", paths->dst_path);
			closedir(src_dir);
			free_paths_pair(paths);
			set_error_and_interrupt_queue_wait(MKDIR_ERROR);
			pthread_exit((void*)MKDIR_ERROR);
		}
	}

	struct dirent* dir_entry = NULL;

	long return_status = 0;

	while (NULL != (dir_entry = readdir(src_dir))) {
		if ((!strcmp(dir_entry->d_name, ".")) || 
			(!strcmp(dir_entry->d_name, ".."))) {
			continue;
		}
		struct stat stat;
		char* cur_path = append_name_to_path(paths->src_path, dir_entry->d_name);
		if (NULL == cur_path) {
			fprintf(stderr, "Not enough memory\n");
			return_status = MEMORY_ALLOCATION_ERROR;
			break;
		}
		if (lstat(cur_path, &stat)) {
			fprintf(stderr, "Stat error: %s\n", cur_path);
			free(cur_path);
			return_status = STAT_ERROR;
			break;
		}
		if (!(S_ISREG(stat.st_mode) || S_ISDIR(stat.st_mode))) {
			free(cur_path);
			continue;
		}
		paths_pair* next_paths = malloc(sizeof(*next_paths));
		if (NULL == next_paths) {
			fprintf(stderr, "Not enough memory\n");
			free(cur_path);
			return_status = MEMORY_ALLOCATION_ERROR;
			break;
		}
		char* cur_new_path = append_name_to_path(paths->dst_path, dir_entry->d_name);
		if (NULL == cur_new_path) {
			fprintf(stderr, "Not enough memory\n");
			free(cur_path);
			free(next_paths);
			return_status = MEMORY_ALLOCATION_ERROR;
			break;
		}
		next_paths->src_path = cur_path;
		next_paths->dst_path = cur_new_path;
		if (S_ISDIR(stat.st_mode)) {
			if (queue_push_task(queue, DIRECTORY, next_paths)) {
				return_status = QUEUE_ERROR;
				break;
			}
		}
		else if (S_ISREG(stat.st_mode)) {
			if (queue_push_task(queue, REGULAR_FILE, next_paths)) {
				return_status = QUEUE_ERROR;
				break;
			}
		}
	}
	if (0 != return_status) {
		set_error_and_interrupt_queue_wait(return_status);
	}
	closedir(src_dir);
	free_paths_pair(paths);
	pthread_exit((void*)return_status);
}

int main(int argc, char* argv[]) {
	if (3 > argc) {
		fprintf(stderr, "Need source and destination arguments\n");
		pthread_exit((void*)MISSING_ARGUMENT);
	}

	pthread_mutex_init(&error_mutex, NULL);

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

	queue = new_queue();
	if (NULL == queue) {
		fprintf(stderr, "Not enough memory\n");
		free_paths_pair(paths);
		pthread_exit((void*)MEMORY_ALLOCATION_ERROR);
	}

	if (queue_push_task(queue, DIRECTORY, paths)) {
		fprintf(stderr, "Queueing error\n");
		free_paths_pair(paths);
		free_queue(queue);
		pthread_exit((void*)QUEUE_ERROR);
	}

	size_t expected_tasks = 1;
	long return_status = 0;

	while ((0 < expected_tasks) && (0 == get_error())) {
		copy_task task;
		if (queue_pop(queue, &task)) {
			fprintf(stderr, "Queue error\n");
			return_status = QUEUE_ERROR;
			break;
		}
		if (0 != get_error()) {
			return_status = get_error();
			break;
		}
		if (0 > task.children) {
			fprintf(stderr, "Task error\n");
			return_status = TASK_ERROR;
			break;
		}
		expected_tasks--;
		expected_tasks += task.children;
		switch (task.type) {
			case DIRECTORY: {
				pthread_t thread;
				int creation_result = 
					pthread_create_detached_with_retry(RESOURCES_WAITING_DELAY_NANOSECONDS,
														&thread, copy_directory, task.paths);
				if (0 != creation_result) {
					fprintf(stderr, "Can't create thread for %s\n", task.paths->src_path);
					free_paths_pair(task.paths);
					return_status = PTHREAD_CREATE_ERROR;
				}
			}
			break;
			case REGULAR_FILE: {
				pthread_t thread;
				int creation_result = 
					pthread_create_detached_with_retry(RESOURCES_WAITING_DELAY_NANOSECONDS,
														&thread, copy_file, task.paths);
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
	free_queue(queue);
	pthread_exit((void*)return_status);
}
