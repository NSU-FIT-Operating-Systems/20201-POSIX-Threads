#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>

#define PHILOSOPHERS_NUM 5
#define DELAY 10000
#define FOOD 50

pthread_mutex_t forks[PHILOSOPHERS_NUM];
pthread_mutex_t foodlock;


typedef struct {
	pthread_t thread;
	int *id;
	bool need_join;
} philosopher_t;


void free_philo(philosopher_t *philo) {
	if (philo != NULL) {
		if (philo->id != NULL) {
			free(philo->id);
		}
		free(philo);
	}
}


int food_on_table() {
	static int food = FOOD;
	int myfood;

	pthread_mutex_lock(&foodlock);
	if (food > 0) {
		--food;
	}
	myfood = food;
	pthread_mutex_unlock(&foodlock);
	return myfood;
}

void get_fork(int fork) {
	pthread_mutex_lock(&forks[fork]);
}

void down_forks(int f1, int f2) {
	pthread_mutex_unlock(&forks[f1]);
	pthread_mutex_unlock(&forks[f2]);
}

void *philosopher(void *num) {
	int id = *(int*)num;
	printf("Philosopher %d sitting down to dinner.\n", id + 1);
	int left_fork = id;
	int right_fork = id + 1;
	int food;
	int counter = 0;

	if (right_fork == PHILOSOPHERS_NUM) {
		right_fork = id;
		left_fork = 0;
	}

	while ((food = food_on_table()) != 0) {
		++counter;
		printf("Philosopher %d: food remain: %d\n", id + 1, food);
		get_fork(left_fork);
		printf("Philosopher %d: got %s fork %d\n", id + 1, "left", left_fork);
		get_fork(right_fork);
		printf("Philosopher %d: got %s fork %d\n", id + 1, "right", right_fork);

		printf("Philosopher %d: eating.\n", id + 1);
		usleep(DELAY * (FOOD - food + 1));
		down_forks (left_fork, right_fork);
	}
	printf("Philosopher %d is done eating.\n", id + 1);
	printf("Philosopher %d has eaten %d dishes\n", id + 1, counter);
	pthread_exit(NULL);
}

int main() {
	if (pthread_mutex_init(&foodlock, NULL) != 0) {
		perror("foodlock mutex init error");
		return 1;
	}
	for (int i = 0; i < PHILOSOPHERS_NUM; ++i) {
		if (pthread_mutex_init(&forks[i], NULL) != 0) {
			perror("Fork mutex init error");
			return 2;
		}
	}

	philosopher_t *phils[PHILOSOPHERS_NUM];

	for (int i = 0; i < PHILOSOPHERS_NUM; ++i) {
		philosopher_t *philo = (philosopher_t *)malloc(sizeof(philosopher_t));
		philo->need_join = true;
		phils[i] = philo;
		if (philo == NULL) {
			perror("Coulnd't allocate philo");
			continue;
		}
		philo->id = (int *)malloc(sizeof(int));
		if (philo->id == NULL) {
			perror("Couldn't allocate philo id");
			philo->need_join = false;
			continue;
		}
		*philo->id = i;
		if (pthread_create(&philo->thread, NULL, philosopher, (void *)philo->id) != 0) {
			perror("Creation philo thread error");
			philo->need_join = false;
		}
	}

	for (int i = 0; i < PHILOSOPHERS_NUM; ++i) {
		if (phils[i] != NULL) {
			if (phils[i]->need_join) {
				if (pthread_join(phils[i]->thread, NULL) != 0) {
					perror("Couldn't join threads");
				}
			}
			free_philo(phils[i]);
		}
	}

	for (int i = 0; i < PHILOSOPHERS_NUM; ++i) {
		pthread_mutex_destroy(&forks[i]);
	}
	pthread_mutex_destroy(&foodlock);

	return 0;
}
