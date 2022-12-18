#include "cpy.h"

#include <string.h>
#include <sys/types.h>
#include <sys/resource.h>
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

#include "dynarr.h"

#define BUF_SIZE 4096 // completely arbitrary
#define STACK_SIZE PTHREAD_STACK_MIN // also completely arbitrary, but, by default, it's 8mb on my 4gb rpi which is insane
#define SLEEP_MS 250

// i want to scream
// LOUD

DynArray* workStack;


// stats for funny
typedef struct glob_t {
	int active_threads;
} Globals;

Globals globals;

typedef struct work_t {
	char* src;
	char* dest;

	pthread_t thread;

	bool isDir;
} ThreadWork;

void freeWork(ThreadWork* wrk) {
	free(wrk->src);
	free(wrk->dest);
	free(wrk);
}

pthread_mutex_t queueMtx;
pthread_cond_t cv;

// wrapper around the queue to guarantee thread-safety
// since the queue by itself isn't
void pushWork(ThreadWork* wrk) {
	pthread_mutex_lock(&queueMtx);
		size_t prevSz = daSize(workStack);

		daPush(workStack, wrk);
		if (prevSz == 0)
			pthread_cond_signal(&cv);

	pthread_mutex_unlock(&queueMtx);
}

void finishThread() {
	__atomic_fetch_sub(&globals.active_threads, 1, __ATOMIC_SEQ_CST);
}

void broadcastFinish() {
	finishThread();

	pthread_mutex_lock(&queueMtx);
		pthread_cond_signal(&cv);
	pthread_mutex_unlock(&queueMtx);
}

int createThreadWork(ThreadWork* wrk, bool alreadyLocked);

// blegh
pthread_attr_t attr;
static bool initted = false;

static void firstInit() {
	if (!initted) {
		initted = true;

		workStack = daNew(64, NULL);

		pthread_mutex_init(&queueMtx, NULL);
		pthread_cond_init(&cv, NULL);

		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		pthread_attr_setstacksize(&attr, STACK_SIZE);

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
			// EMFILE, EAGAIN returned if opendir fails - means we need to push the work ourselves
			// EAGAIN 	returned if we couldn't create thread(s)
			//			for the files in the directory; already handles the work

			if (ok == EMFILE || ok == ENFILE || ok == EAGAIN) {
				finishThread();
				if (ok != EAGAIN) {
					pushWork(work);
				}
				return NULL;
			} else {
				// not a normal error
				printf("error during cpydir (%d): ", ok);
				perror("");
				goto cleanup;
			}
		}
	} else {
		// welcome to the dining philosophers problem
		int fdRead = open(work->src, O_RDONLY);

		if (fdRead == -1) {
			if (errno == EMFILE) {
				// We couldn't open the file _yet_; just push our work back onto the work queue and exit
				finishThread();
				pushWork(work);
				return NULL;
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

		int fdWrite = open(work->dest, O_WRONLY | O_CREAT, perms);
		if (fdWrite == -1) {
			close(fdRead); // close and wait until both are available

			if (errno == EMFILE) {
			 	finishThread();
				pushWork(work);
				return NULL;
			} else {
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
	broadcastFinish();

	return NULL;
}

int createThreadWork(ThreadWork* wrk, bool alreadyLocked) {
	int ok = pthread_create(&wrk->thread, &attr, thread_cpyDir, wrk);
	if (ok == 0) { // success
		__atomic_fetch_add(&globals.active_threads, 1, __ATOMIC_SEQ_CST);
		return 0;
	}

	if (ok == EAGAIN) {
		// thread limit; put ourselves onto the work queue and die
		if (alreadyLocked) {
			daPush(workStack, wrk);
		} else {
			pushWork(wrk);
		}
	} else {
		perror("failed to create copy thread");
	}

	return ok;
}

bool shouldSkip(const char* path) {
	return strcmp(path, ".") == 0 || strcmp(path, "..") == 0;
}

int cpyDir(const char* from, const char* to) {
	firstInit();

	int dirOk = mkdir(to, 0777);
	if (dirOk != 0 && errno != EEXIST) {
		printf("failed to mkdir: ");
		perror("");
		return dirOk;
	}

	int fromLen = strlen(from);

	DIR* inDir = opendir(from);

	if (inDir == NULL) {
		if (errno == EMFILE || errno == ENFILE) {
			// Just return this error; this is normal operation
			// Whoever called us should handle it
			return errno;
		} else {
			printf("error while opening dir (%s): ", from);
			perror("");
			return errno;
		}
	}

	struct dirent* result;

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
			closedir(inDir);
			return errno;
		}

		wrk->src = NULL;
		wrk->dest = NULL;

		errno = 0;

		size_t len = strlen(result->d_name);

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

		int ok = createThreadWork(wrk, false);
		// EAGAIN merely means we'll run later so we'll still need the data in work
		// Any other error means we failed and the resources won't be used anymore
		if (ok != 0 && ok != EAGAIN) goto cleanup_thread;

		i++;

		continue;

		// this isn't ran under normal circumstances; only goto'd if an error occurs during resource init
cleanup_thread:
		if (wrk->src != NULL) 	free(wrk->src);
		if (wrk->dest != NULL) 	free(wrk->dest);
		free(wrk);
		printf("cleanup_thread occured; this is bad!!!\n");
		break;
	}

	/* Cleanup */
	closedir(inDir);

	return errno;
}

int main(int argc, char** argv) {
	int err = cpyDir("test/", "test_cpy/");
	if (err) {
		perror("error from cpyDir: ");
	}

	struct timespec ts;

	pthread_mutex_lock(&queueMtx);

	while (1) {
		// we may wake up to an empty queue (broadcastFinish)
		while (daSize(workStack) == 0) sleep: {
			int threads = __atomic_load_n(&globals.active_threads, __ATOMIC_RELAXED);
			// Double-check workStack size; we may end up here from a goto
			// 	while the stack isn't actually empty
			if (threads == 0 && daSize(workStack) == 0) goto exit;

			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_nsec += SLEEP_MS * 1e6;
			if (ts.tv_nsec > 1e9) {
				ts.tv_nsec -= 1e9;
				ts.tv_sec += 1;
			}

			pthread_cond_timedwait(&cv, &queueMtx, &ts);
		}

		// int threads = __atomic_load_n(&globals.active_threads, __ATOMIC_RELAXED);
		// printf("have %d works... %d active threads.\n", daSize(workStack), threads);

		int created = 0;

		// Create as many threads as we can before sleeping again
		while (daSize(workStack) != 0) {
			ThreadWork* wrk = daPop(workStack);

			int ok = createThreadWork(wrk, true);
			if (ok != 0) {
				// printf("Created %d before dying (%s)\n", created, strerror(ok));
				goto sleep; // just unlock and wait until someone else queues something up
			}
			created++;
		}
	}
exit:
	printf("Finished! %d works\n", daSize(workStack));
	pthread_mutex_unlock(&queueMtx);

	pthread_exit(NULL);
	daFree(workStack);
}
