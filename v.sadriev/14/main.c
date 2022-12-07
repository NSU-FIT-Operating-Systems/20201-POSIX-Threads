#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <errno.h>

#define ITER_NUM 10
#define SEMS_NUM 2

sem_t sems[SEMS_NUM];

int sem_wait_anti_spurious(sem_t *sem) {
  int err;
  do {
	errno = 0;
	err = sem_wait(sem);
  } while (err == -1 && errno == EINTR);
  return err;
}

void unlock_all_sems() {
  for (size_t i = 0; i < SEMS_NUM; ++i) {
	if (sem_post(&sems[i]) != 0) {
	  perror("Post error while unlocking all sems");
	  break;
	}
  }
}

void destroy_all_sems() {
  for (size_t i = 0; i < SEMS_NUM; ++i) {
	if (sem_destroy(&sems[i]) != 0) {
	  perror("Destroying sems error");
	}
  }
}

int run_printing(size_t self_id, size_t next_id, char *to_print) {
  assert(to_print != NULL);
  int err;
  for (size_t i = 0; i < ITER_NUM; ++i) {
	printf("%zu: %s\n", i + 1, to_print);
	if ((err = sem_post(&sems[next_id])) != 0) {
	  break;
	}
	if ((err = sem_wait_anti_spurious(&sems[self_id])) != 0) {
	  break;
	}
  }
  return err;
}

void *print_message(void *args) {
  assert(args != NULL);
  int err;
  size_t id = (size_t)args; // == 1
  size_t next_id = (id + 1) % SEMS_NUM;
  char *to_print = "New thread is cool!(child)";

  if ((err = sem_wait_anti_spurious(&sems[id])) != 0) {
	perror("Initial wait error");
	pthread_exit(&err);
  }

  if ((err = run_printing(id, next_id, to_print)) != 0) {
	perror("Error while printing(wait or post error)");
	pthread_exit(&err);
  }
  unlock_all_sems();
  pthread_exit(&err);
}

int init_all_sems() {
  int err = 0;
  for (size_t i = 0; i < SEMS_NUM; ++i) {
	if ((err = sem_init(&sems[i], 0, 0)) != 0) {
	  for (size_t j = 0; j < i; ++j) {
		sem_destroy(&sems[j]);
	  }
	  break;
	}
  }
  return err;
}

int main() {
  int err;
  size_t main_id = 0;
  size_t new_thread_id = 1;
  pthread_t newThread;
  char *to_print = "Main(old) thread is cool!(parent)";

  if ((err = init_all_sems()) != 0) {
	perror("Couldn't init sems");
	pthread_exit(&err);
  }

  if ((err = pthread_create(&newThread, NULL, &print_message, (void *)new_thread_id)) != 0) {
	perror("Pthread creation error");
	destroy_all_sems();
	pthread_exit(&err);
  }

  if ((err = run_printing(main_id, new_thread_id, to_print)) != 0) {
	perror("Error while printing(wait or post error)");
	pthread_exit(&err);
  }
  unlock_all_sems();

  if ((err = pthread_join(newThread, NULL)) != 0) {
	perror("Couldn't join threads");
	destroy_all_sems();
	pthread_exit(&err);
  }

  destroy_all_sems();
  pthread_exit(&err);
}
