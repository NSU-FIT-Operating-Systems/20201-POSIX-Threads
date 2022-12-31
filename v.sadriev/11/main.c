#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define ITER_NUM 11
#define MUTEXES_NUM 3

pthread_mutex_t mutexes[MUTEXES_NUM];

void *print_message(void *to_print) {
  pthread_mutex_lock(&mutexes[2]);
  pthread_mutex_lock(&mutexes[0]);
  int i;
  for (i = 0; i < ITER_NUM; ++i) {
	printf("%d: %s\n", i, (char *)to_print);
	if (pthread_mutex_unlock(&mutexes[(i + 2) % MUTEXES_NUM]) != 0) {
	  perror("Unlock fail new thread");
	}
	if (pthread_mutex_lock(&mutexes[(i + 1) % MUTEXES_NUM]) != 0) {
	  perror("Lock fail new thread");
	}
  }
  pthread_mutex_unlock(&mutexes[(i + 2) % MUTEXES_NUM]);
  pthread_mutex_unlock(&mutexes[i % MUTEXES_NUM]);
  return NULL;
}

int main() {
  pthread_t newThread;
  char *new_thread_string = "New thread is cool!";
  char *main_thread_string = "Main(old) thread is cool(base)!";
	pthread_mutexattr_t attr;
	if (pthread_mutexattr_init(&attr) != 0) {
		perror("Attr init error");
		return 1;
	}
	if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) != 0) {
		perror("attr settype error");
		return 2;
	}

//todo change attr
  for (int i = 0; i < MUTEXES_NUM; ++i) {
	if (pthread_mutex_init(&mutexes[i], &attr) != 0) {
	  perror("Init mutex error");
	  return 3;
	}
  }

  pthread_mutex_lock(&mutexes[0]);
	sleep(1);
  pthread_mutex_lock(&mutexes[1]);

  if (pthread_create(&newThread, NULL, &print_message, (void *)new_thread_string) != 0) {
	perror("Pthread creation error");
	return -1;
  }

  int i;
  for (i = 0; i < ITER_NUM; ++i) {
	printf("%d: %s\n", i, main_thread_string);
	if (pthread_mutex_unlock(&mutexes[i % MUTEXES_NUM]) != 0) {
	  perror("Main unlock fail");
	}
	if (pthread_mutex_lock(&mutexes[(i + 2) % MUTEXES_NUM]) != 0) {
	  perror("Main lock fail");
	}
  }
  // i == ITER_NUM
  pthread_mutex_unlock(&mutexes[i % MUTEXES_NUM]);
  pthread_mutex_unlock(&mutexes[(i + 1) % MUTEXES_NUM]);

  if (pthread_join(newThread, NULL) != 0) {
	perror("Couldn't join threads");
	pthread_exit(NULL);
  }

	if (pthread_mutexattr_destroy(&attr) != 0) {
		perror("Attr destroy error");
	}
  for (i = 0; i < MUTEXES_NUM; ++i) {
	if (pthread_mutex_destroy(&mutexes[i]) != 0) {
	  perror("Mutex destroy error");
	}
  }

  return 0;
}
