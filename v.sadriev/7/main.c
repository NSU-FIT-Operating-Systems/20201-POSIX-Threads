#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "copy_queue.h"

#define SLEEP_INTERVAL_MICROSEC (1000 * 10)
#define BUF_SIZE 4096

enum ERROR {
	E_MISS_ARG = 1,
	E_MEM_ALLOC,
	E_ATTR,
	E_TASK,
	E_THREAD,
	E_OPEN,
	E_READ,
	E_WRITE,
	E_CLOSE,
	E_MDIR,
};

queue_t *queue = NULL;
bool interrupt = false;

static bool init_detached_attr(pthread_attr_t *detach_attr) {
	if (pthread_attr_init(detach_attr) != 0) {
		return true;
	}
	if (pthread_attr_setdetachstate(detach_attr, PTHREAD_CREATE_DETACHED) != 0) {
		return true;
	}
	return false;
}

int open_with_retry(char *name, int flags, mode_t mode) {
	int fd;
	while (true) {
		if ((fd = open(name, flags, mode)) == -1) {
			if (errno == ENFILE || errno == EMFILE) {
				usleep(SLEEP_INTERVAL_MICROSEC);
				continue;
			}
			break;
		} else {
			break;
		}
	}
	return fd;
}

int close_fd(int fd) {
	if (fd >= 0) {
		return close(fd);
	}
	return fd;
}

void *copy_reg(void *arg) {
	paths_t *paths = (paths_t *)arg;
	assert(paths != NULL);
	long err = 0;
	int src_fd = -1;
	int dst_fd = -1;

	if ((src_fd = open_with_retry(paths->src, O_RDONLY, 0)) < 0) {
		fprintf(stderr, "Couldn't open src file %s: ", paths->src);
		perror("");
		err = E_OPEN;
		interrupt = true;
		goto quit;
	}

	struct stat status;
	if (fstat(src_fd, &status) != 0) {
		perror("Couldn't stat");
		err = E_OPEN;
		interrupt = true;
		goto quit;
	}

	if ((dst_fd = open_with_retry(paths->dst, O_WRONLY | O_CREAT, status.st_mode)) < 0) {
		fprintf(stderr, "Couldn't open dst file %s: ", paths->dst);
		perror("");
		err = E_OPEN;
		interrupt = true;
		goto quit;
	}

	void *buf = malloc(BUF_SIZE * sizeof(char));
	if (buf == NULL) {
		perror("Couldn't alloc memory for buffer");
		err = E_MEM_ALLOC;
		goto quit;
	}
	long read_bytes = 0;
	long write_bytes = 0;
	while ((read_bytes = read(src_fd, buf, BUF_SIZE)) > 0) {
		if ((write_bytes = write(dst_fd, buf, read_bytes)) == -1) {
			fprintf(stderr, "read bytes(%ld) != write_bytes(%ld)\n", read_bytes, write_bytes);
			perror("Write error");
			err = E_WRITE;
			goto quit;
		}
	}
	free(buf);
	if (read_bytes < 0) {
		perror("Read error");
		err = E_READ;
		goto quit;
	}

	if (close_fd(src_fd) != 0 || close_fd(dst_fd) != 0) {
		perror("Close error");
		err = E_CLOSE;
		goto quit;
	}

	free_paths(paths);
	pthread_exit(NULL);
quit:
	close_fd(src_fd);
	close_fd(dst_fd);
	free_paths(paths);
	interrupt = true;
	pthread_exit((void *)err);
}

static char *append_path(char *path, char *to_append) {
	assert(path != NULL);
	assert(to_append != NULL);

	size_t path_len = strlen(path);
	size_t to_append_len = strlen(to_append);
	size_t slash_len = (path[path_len - 1] == '/') ? 0 : 1;
	size_t res_len = path_len + to_append_len + slash_len + 1;
	char *res_path = (char *)malloc(res_len * sizeof(char));
	if (res_path == NULL) {
		return NULL;
	}

	strcpy(res_path, path);
	if (res_path[strlen(res_path) - 1] != '/') {
		strcat(res_path, "/");
	}
	strcat(res_path, to_append);
	return res_path;
}

copy_task_t *create_task_from_dir(struct dirent *entry, paths_t *paths) {
	assert(entry != NULL);
	struct stat status;
	char *cur_path = append_path(paths->src, entry->d_name);
	if (cur_path == NULL) {
		perror("Couldn't allocate mem for src path");
		interrupt = true;
		return NULL;
	}
//	printf("cur path : %s\n", cur_path);
	if (stat(cur_path, &status) != 0) {
		perror("Couldn't get file status");
		free(cur_path);
		interrupt = true;
		return NULL;
	}

	if (!(S_ISREG(status.st_mode) || S_ISDIR(status.st_mode))) {
		free(cur_path);
		return NULL;
	}

	char *dst_path = append_path(paths->dst, entry->d_name);
	if (dst_path == NULL) {
		perror("Couldn't allocate memory for dst path");
		interrupt = true;
		return NULL;
	}
//	printf("dst path : %s\n", dst_path);

	paths_t *new_paths = create_paths(cur_path, dst_path);
	if (new_paths == NULL) {
		perror("Couldn't allocate memory for new paths");
		interrupt = true;
		return NULL;
	}
	copy_task_t *task = create_task(S_ISDIR(status.st_mode) ? DIRECTORY : REGULAR_FILE, new_paths);
	if (task == NULL) {
		perror("Couldn't allocate memory for task");
		interrupt = true;
		return NULL;
	}
	return task;
}

void *copy_dir(void *arg) {
	paths_t *paths = (paths_t *)arg;
	assert(arg != NULL);
	long err = 0;
	int src_fd = open_with_retry(paths->src, O_RDONLY, 0);
	if (src_fd < 0) {
		perror("Couldn't open src file");
		err = E_OPEN;
		goto error_without_closedir;
	}

	if (0 != mkdir(paths->dst, 0777)) {
		if (errno != EEXIST) {
			if (errno == EACCES) {
				printf("errno == eacces\n");
			}
			fprintf(stderr, "Couldn't make dir %s: ", paths->dst);
			perror("");
			err = E_MDIR;
			goto error_without_closedir;
		}
	}

	struct dirent *entry;
	DIR *src_dir = fdopendir(src_fd);
	if (src_dir == NULL) {
		perror("Couldn't allocate memory for src_dir");
		err = E_MEM_ALLOC;
		goto error;
	}

	while ((entry = readdir(src_dir)) != NULL) {
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
			continue;
		}
		copy_task_t *task = create_task_from_dir(entry, paths);
		if (task == NULL) {
			if (interrupt) {
				perror("Couldn't create task");
				err = E_MEM_ALLOC;
				goto error;
			}
			continue;
		}
		if (push(queue, task)) {
			perror("Couldn't push task");
			err = E_MEM_ALLOC;
			goto error;
		}
	}

	closedir(src_dir);
	free_paths(paths);
	pthread_exit((void *)err);
error:
	closedir(src_dir);
error_without_closedir:
	free_paths(paths);
	interrupt = true;
	pthread_exit((void *)err);

}

int create_copy_thread(pthread_attr_t *attr, enum TASK_TYPE type, paths_t *paths) {
	pthread_t new_thread;
	int res;
	while (true) {
		if ((res = pthread_create(&new_thread, attr, type == DIRECTORY ? copy_dir : copy_reg, paths)) != 0) {
			if (res == EAGAIN) {
				usleep(SLEEP_INTERVAL_MICROSEC);
				continue;
			}
			return E_THREAD;
		}
		return 0;
	}
}

//todo remove?
int count_dir_files(char *pathname) {
	assert(pathname != NULL);
	int res = 0;
	struct dirent *entry;
	DIR *dir = opendir(pathname);
	if (dir == NULL) {
		return -1;
	}
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type == DT_DIR || entry->d_type == DT_REG) {
			++res;
		}
	}
	return res;
}

bool wait_tasks() {
	size_t i = 0;
	size_t limit = 20;
	while (is_empty(queue)) {
//		printf("queue is empty? %d\n", is_empty(queue));
		if (i > limit) {
			return true;
		}
		usleep(SLEEP_INTERVAL_MICROSEC);
		++i;
	}
	return false;
}

int main(int argc, char *argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Pass src and dst paths as args\n");
		pthread_exit((void *)E_MISS_ARG);
	}

	pthread_attr_t detach_attr;
	if (init_detached_attr(&detach_attr)) {
		perror("Couldn't init attr and set detach state");
		pthread_exit((void *)E_ATTR);
	}

	long err = 0;
	paths_t *paths = NULL;
	copy_task_t *first_task = NULL;

	if ((paths = create_paths(strdup(argv[1]), strdup(argv[2]))) == NULL) {
		perror("Couldn't allocate init paths");
		err = E_MEM_ALLOC;
		goto release_res;
	}

	if ((queue = create_queue()) == NULL) {
		perror("Couldn't allocate queue");
		err = E_MEM_ALLOC;
		goto release_res;
	}

	if ((first_task = create_task(DIRECTORY, paths)) == NULL) {
		perror("Couldn't create task");
		free_paths(paths);
		err = E_MEM_ALLOC;
		goto release_res;
	}

	if (push(queue, first_task)) {
		perror("Couldn't push task to queue");
		free_task(first_task);
		err = E_MEM_ALLOC;
		goto release_res;
	}
//	printf("queue size: %zu\n", queue->size);

	while (true) {
		if (wait_tasks() || interrupt) {
			break;
		}
		copy_task_t *cur_task = pop(queue);
		if (cur_task == NULL) {
			perror("Couldn't pop task from queue");
			interrupt = true;
			free_task(cur_task);
			err = E_TASK;
			goto release_res;
		}
		if (create_copy_thread(&detach_attr, cur_task->type, cur_task->paths) != 0) {
			perror("Couldn't create new thread");
			interrupt = true;
			free_task(cur_task);
			err = E_THREAD;
			goto release_res;
		}
		free(cur_task);
	}

release_res:
	free_queue(queue);
	pthread_attr_destroy(&detach_attr);
	pthread_exit((void *)err);
}
