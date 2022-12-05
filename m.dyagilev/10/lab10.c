#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define PHILO 5
#define DELAY 1000
#define FOOD 50

pthread_mutex_t forks[PHILO];
pthread_t phils[PHILO];
pthread_mutex_t food_lock;

int sleep_seconds = 0;

int food_on_table() {
	static int food = FOOD;
	int my_food;

	if (pthread_mutex_lock(&food_lock)) {
		fprintf(stderr, "Locking error\n");
	}

	my_food = food;
	if (food > 0) {
		food--;
	}

	if (pthread_mutex_unlock(&food_lock)) {
		fprintf(stderr, "Unocking error\n");
	}

	return my_food;
}

void get_fork(size_t phil, int fork, char* hand) {
	if (pthread_mutex_lock(&forks[fork])) {
		fprintf(stderr, 
			"ERROR: Philosopher %zu: not got %s fork %d\n", phil, hand, fork);
	}
	printf("Philosopher %zu: got %s fork %d\n", phil, hand, fork);
}

void down_forks(int f1, int f2) {
	if (pthread_mutex_unlock(&forks[f1])) {
		fprintf(stderr, "Unocking %d error\n", f1);
	}
	if (pthread_mutex_unlock(&forks[f2])) {
		fprintf(stderr, "Unocking %d error\n", f2);
	}
}

void* philosopher(void* arg) {
	size_t id = (size_t)arg;

	printf ("Philosopher %zu sitting down to dinner.\n", id);

	int right_fork = id;
	int left_fork = (id + 1) % PHILO;
	int min_num_fork = (right_fork < left_fork) ? right_fork : left_fork;
	int max_num_fork = (right_fork < left_fork) ? left_fork : right_fork;
	int food_num = 0;
	int eaten = 0;

	while (0 != (food_num = food_on_table())) {
		printf("Philosopher %zu: thinking.\n", id);
		usleep(DELAY);

		printf("Philosopher %zu: get dish %d.\n", id, food_num);
		get_fork(id, min_num_fork, (min_num_fork == right_fork) ? "right" : "left");
		get_fork(id, max_num_fork, (max_num_fork == left_fork) ? "left" : "right");

		printf("Philosopher %zu: eating.\n", id);
		usleep(DELAY);
		down_forks(left_fork, right_fork);
		eaten++;
	}
	printf("Philosopher %zu is done eating(%d)\n", id, eaten);
	pthread_exit(0);
}

int main () {
	pthread_mutex_init (&food_lock, NULL);
	for (size_t i = 0; i < PHILO; i++) {
		pthread_mutex_init(&forks[i], NULL);
	}
	for (size_t i = 0; i < PHILO; i++) {
		int creation_result = pthread_create(&phils[i], NULL, philosopher, 
											(void *)i);
		if (0 != creation_result) {
			fprintf(stderr, "Main error: can't create thread for %zu, result = %d\n", 
					i , creation_result);
		}
	}
	for (size_t i = 0; i < PHILO; i++) {
		int join_result = pthread_join(phils[i], NULL);
		if (0 != join_result) {
			fprintf(stderr, "Main error: can't join thread for %zu, result = %d\n", 
					i, join_result);
		}
	}
	pthread_mutex_destroy(&food_lock);
	for (size_t i = 0; i < PHILO; i++) {
		pthread_mutex_destroy(&forks[i]);
	}
	pthread_exit(0);
}
