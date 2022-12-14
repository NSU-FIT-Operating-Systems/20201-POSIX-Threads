#include "cpy.h"

#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <dirent.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "dirs.h"

#define BUF_SIZE 8192 // completely arbitrary
#define STACK_SIZE 65536 // also completely arbitrary, but, by default, it's 8mb on my 4gb rpi which is insane

// stats for funny
typedef struct glob_t {
	int EMFILE_counter;
	int EAGAIN_counter;
	int active_threads;
} Globals;

Globals globals;

typedef struct work_t {
	char* src;
	char* dest;
	pthread_mutex_t sync;
	pthread_t thread;

	bool isDir;
} ThreadWork;

void freeWork(ThreadWork* wrk) {
	free(wrk->src);
	free(wrk->dest);
	pthread_mutex_destroy(&wrk->sync);
	free(wrk);
}

// blegh
pthread_attr_t attr;
static bool initted = false;

static void firstInit() {
	if (!initted) {
		initted = true;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

		pthread_attr_setstacksize(&attr, STACK_SIZE);

		globals.EMFILE_counter = 0;
		globals.EAGAIN_counter = 0;
		globals.active_threads = 0;
	}
}

void* thread_cpyDir(void* wrk) {
	ThreadWork* work = (ThreadWork*)wrk;

	// printf("thread: copy %s %s -> %s\n",
	// 	work->isDir ? "directory" : "file", work->src, work->dest);

	if (work->isDir) {
		int ok = cpyDir(work->src, work->dest);
		if (ok != 0) {
			printf("error during cpydir: ");
			perror("");
			goto cleanup;
		}
	} else {
		int fdRead;

		while (1) {
			fdRead = open(work->src, O_RDONLY);
			if (fdRead != -1) break;

			if (errno == EMFILE) {
				__atomic_fetch_add(&globals.EMFILE_counter, 1, __ATOMIC_SEQ_CST);
				sleep(1);
			} else {
				printf("error while opening source file (%s): ", work->src);
				perror("");
				goto cleanup;
			}
		}

		struct stat st;
		int perms = 0;

		if (fstat(fdRead, &st)) {
			printf("error while attempting to get source file permissions (%s): ", work->src);
			perror("");
			printf("defaulting to 0777");
			perms = 777;
		} else {
			perms = st.st_mode & 07777;
		}

		int fdWrite;
		while (1) {
			fdWrite = open(work->dest, O_WRONLY | O_CREAT, perms);
			if (fdWrite != -1) break;

			 if (errno == EMFILE) {
			 	__atomic_fetch_add(&globals.EMFILE_counter, 1, __ATOMIC_SEQ_CST);
				sleep(1);
			} else {
				close(fdRead);
				printf("error while opening destination file (%s): ", work->dest);
				perror("");
				goto cleanup;
			}
		}

		char buf[BUF_SIZE];
		int a;

		while (1) {
			a = read(fdRead, buf, BUF_SIZE);
			if (a == 0)
				break; // EOF

			if (a == -1) {
				printf("error while reading from source file (%s): ", work->src);
				perror("");
				goto cleanupFiles;
			}

			while (a > 0) {
				int ok = write(fdWrite, buf, a);
				if (ok == -1) {
					printf("error while wrting to destination file (%s): ", work->dest);
					perror("");
					goto cleanupFiles;
				}

				a -= ok;
			}
		}

cleanupFiles:
		close(fdRead);
		close(fdWrite);
	}

cleanup:
	freeWork(wrk);
	__atomic_fetch_sub(&globals.active_threads, 1, __ATOMIC_SEQ_CST);

	// printf("EMFILE: %d, EAGAIN: %d\n", globals.EMFILE_counter, globals.EAGAIN_counter);

	return NULL;
}

bool shouldSkip(const char* path) {
	return strcmp(path, ".") == 0 || strcmp(path, "..") == 0;
}

int cpyDir(const char* from, const char* to) {
	firstInit();
	recursive_mkdir(to);

	int fromLen = strlen(from);

	DIR* inDir;
	while (1) {
		inDir = opendir(from);
		if (inDir != NULL) break;

		if (errno == EMFILE || errno == ENFILE) {
			__atomic_fetch_add(&globals.EMFILE_counter, 1, __ATOMIC_SEQ_CST);
			sleep(1);
		} else {
			printf("error while opening dir (%s): ", from);
			perror("");
			return errno;
		}
	}

	struct dirent* result;

	/*
	struct dirent entry;
	char pathBuf[_POSIX_PATH_MAX + 1]; // man 3 pathcon
	pathBuf[sizeof(pathBuf) - 1] = 0;
	int err = 0;
	while ((err = readdir_r(inDir, &entry, &result)) == 0 && result != NULL) {
		printf("awooga\n");
	}
	*/

	errno = 0;
	int count = 0;
	int count_without_garbage = 0;

	// 1. Count the amount of subdirectories
	while ((result = readdir(inDir)) != NULL) {
		count++;

		if (shouldSkip(result->d_name))
			continue;

		count_without_garbage++;
	}

	if (count_without_garbage == 0) {
		closedir(inDir);
		return 0;
	}

	// 2. Rewind
	rewinddir(inDir);

	// 3. Allocate for `count_without_garbage` of threads
	// char* str = result->d_name;
	int i = 0;

	for (int iSux = 0; iSux < count; iSux++) {
		result = readdir(inDir);
		if (result == NULL) {
			// i bet this can happen, too... cringe
			printf("this isnt supposed to happen: directory structure changed mid-way through iterations!?!?");
			errno = 0;
			return -1;
		}

		if (shouldSkip(result->d_name))
			continue;


		ThreadWork* wrk = malloc(sizeof(ThreadWork));
		if (wrk == NULL) {
			return errno;
		}

		int ok = pthread_mutex_init(&wrk->sync, NULL);
		if (ok != 0) {
			perror("error while initializing mutex(!?):");
			goto cleanup;
		}

		wrk->src = NULL;
		wrk->dest = NULL;
		bool madeThread = false;


		errno = 0;
		madeThread = true;
		size_t len = strlen(result->d_name);
		__atomic_fetch_add(&globals.active_threads, 1, __ATOMIC_SEQ_CST);

		wrk->src = malloc(len + 1 + fromLen + 2);
		if (wrk->src == NULL) {
			goto cleanup_thread;
		}

		strcpy(wrk->src, from);
		strcat(wrk->src, result->d_name);

		//                       /                /\0
		wrk->dest = malloc(len + 1 + strlen(to) + 2);
		if (wrk->dest == NULL) {
			goto cleanup_thread;
		}

		strcpy(wrk->dest, to);
		strcat(wrk->dest, result->d_name);

		if (result->d_type == DT_DIR) {
			strcat(wrk->src, "/");
			strcat(wrk->dest, "/");
		}

		assert(result->d_type != DT_UNKNOWN); // please god no
		wrk->isDir = result->d_type == DT_DIR;

		while (1) {
			int ok = pthread_create(&wrk->thread, &attr, thread_cpyDir, wrk);
			if (ok == 0) break;

			if (ok == EAGAIN) {
				// thread limit; maybe if we just sleep a bit
				__atomic_fetch_add(&globals.EAGAIN_counter, 1, __ATOMIC_SEQ_CST);
				printf("sleeping, EAGAIN: %d, active threads: %d\n", globals.EAGAIN_counter, globals.active_threads);
				sleep(1);
				continue;
			} else {
				perror("failed to create copy thread; aborting copy.");
				goto cleanup_thread;
			}
		}

		i++;

		continue;

cleanup_thread:
		if (madeThread) {
			pthread_cancel(wrk->thread);
			__atomic_fetch_sub(&globals.active_threads, 1, __ATOMIC_SEQ_CST);
		}
		if (wrk->src != NULL) 	free(wrk->src);
		if (wrk->dest != NULL) 	free(wrk->dest);
		pthread_mutex_destroy(&wrk->sync);
		free(wrk);
	}

	/* Cleanup */
cleanup:
	closedir(inDir);

	return errno;
}

int main(int argc, char** argv) {
	int err = cpyDir("test/", "test_cpy/");
	if (err) {
		perror("error from cpyDir: ");
	}

	pthread_exit(NULL);
}