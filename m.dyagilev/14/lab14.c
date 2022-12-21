#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <semaphore.h>

enum ERRORS {
  CREATE_ERROR = 1,
  JOIN_ERROR
};

#define LINES_NUM 10
#define SEMS_NUM 2

sem_t sems[SEMS_NUM];

void sems_destroy_all() {
  for (size_t i = 0; i < SEMS_NUM; ++i) {
    sem_destroy(&sems[i]);
  }
}

void unlock_all_sems() {
  for (size_t i = 0; i < SEMS_NUM; ++i) {
    int code = 0;
    if ((code = sem_post(sems + i))) {
      perror("Posting error");
      break;
    }
  }
}

void *func(void *args) {
  long id = (long) args;
  long next_id = (id + 1) % SEMS_NUM;
  long code = 0;

  while (true) {
    if ((code = sem_wait(&sems[id]))) {
      if (EINTR != errno) {
        break;
      }
    } else {
      break;
    }
  }
  if (0 != code) {
    perror("C : Waiting error");
    pthread_exit((void *) code);
  }

  for (size_t i = 0; i < LINES_NUM; ++i) {
    printf("Child: %zu\n", i + 1);

    if ((code = sem_post(&sems[next_id]))) {
      perror("C : Posting error");
      break;
    }

    while (true) {
      if ((code = sem_wait(&sems[id]))) {
        if (EINTR != errno) {
          break;
        }
      } else {
        break;
      }
    }
    if (0 != code) {
      perror("C : Waiting error");
      break;
    }
  }
  unlock_all_sems();

  pthread_exit((void *) code);
}

int main() {
  for (size_t i = 0; i < SEMS_NUM; i++) {
    int result = sem_init(&sems[i], 0, 0);
    if (0 != result) {
      perror("Error while initializing");
      for (size_t j = 0; j < i; ++j) {
        sem_destroy(&sems[i]);
      }
    }
  }

  pthread_t thread;

  int creation_result = pthread_create(&thread, NULL, func, (void *) (long) 1);
  if (0 != creation_result) {
    fprintf(stderr, "Main error: can't create thread, result = %d\n",
            creation_result);
    sems_destroy_all();
    pthread_exit((void *) CREATE_ERROR);
  }

  long code = 0;

  for (size_t i = 0; i < LINES_NUM; ++i) {
    printf("Parent: %zu\n", i + 1);

    if ((code = sem_post(&sems[1]))) {
      perror("P : Posting error");
      break;
    }

    while (true) {
      if ((code = sem_wait(&sems[0]))) {
        if (EINTR != errno) {
          break;
        }
      } else {
        break;
      }
    }
    if (0 != code) {
      perror("P : Waiting error");
      break;
    }
  }
  unlock_all_sems();

  int join_result = pthread_join(thread, NULL);
  if (0 != join_result) {
    fprintf(stderr, "Main error: can't join thread, result = %d\n",
            join_result);
    pthread_exit((void *) JOIN_ERROR);
  }

  sems_destroy_all();

  pthread_exit((void *) code);
}
