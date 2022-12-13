#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

#define PHILO 5
#define USE_ARBITER 1
#define DELAY 50000
#define DELAY_RAND 1
#define FOOD 50

pthread_t phils[PHILO];

pthread_mutex_t forklocks[PHILO];

pthread_mutex_t foodlock;
pthread_mutex_t arbiterlock;

typedef struct philo_t {
	int foodAte;
	int id;
	pthread_t thread;
} Philosopher;


int sleep_seconds = 0;

static int foodLeft = FOOD;

int takeFood(int id) {
	int myfood;

	pthread_mutex_lock(&foodlock);

	myfood = foodLeft;
	if (foodLeft > 0) {
		foodLeft--;
	}

	pthread_mutex_unlock(&foodlock);

	return myfood;
}

void acquire_forks(int id, int left, int right) {
	// printf("lock arbiter\n");
	if (USE_ARBITER) {
		pthread_mutex_lock(&arbiterlock);
	}

	// printf("lock fork right (%d)\n", right);
	pthread_mutex_lock(&forklocks[right]);

	// printf("lock fork left (%d)\n", left);
	pthread_mutex_lock(&forklocks[left]);

	// printf("unlock arbiter\n");

	if (USE_ARBITER) {
		pthread_mutex_unlock(&arbiterlock);
	}
}

void down_forks(int phil, int left, int right) {
	// pthread_mutex_lock(&arbiterlock);

	pthread_mutex_unlock(&forklocks[right]);
	pthread_mutex_unlock(&forklocks[left]);

	// pthread_mutex_unlock(&arbiterlock);
}

void* philThink (void* self_shapeless_form) {
	srand(time(NULL));

	// i identify as a philosopher_t
	Philosopher* self = (Philosopher*)self_shapeless_form;

	int id = self->id;
	int f;

	printf ("Philosopher %d sitting down to dinner.\n", id);

	int left_fork = id + 1;
	int right_fork = id;

	/* Wrap around the forks. */
	if (left_fork == PHILO)
		left_fork = 0;

	while ((f = takeFood(id))) {
		acquire_forks(id, left_fork, right_fork);

		printf ("Philosopher %d: eating. Food left: %d\n", id, f);
		usleep (DELAY + rand() % DELAY_RAND);
		down_forks(id, left_fork, right_fork);

		self->foodAte++;
	}

	printf ("Philosopher %d is done eating.\n", id);
	return (NULL);
}

int main(int argn, char **argv) {
	if (argn == 2)
		sleep_seconds = atoi (argv[1]);

	Philosopher* phils = malloc(PHILO * sizeof(Philosopher));

	pthread_mutex_init (&foodlock, NULL);
	pthread_mutex_init (&arbiterlock, NULL);

	for (int i = 0; i < PHILO; i++)
		pthread_mutex_init (&forklocks[i], NULL);

	for (int i = 0; i < PHILO; i++) {
		Philosopher* phil = &phils[i];
		phil->id = i;
		phil->foodAte = 0;

		pthread_create(&phil->thread, NULL, philThink, phil);
	}

	for (int i = 0; i < PHILO; i++) {
		Philosopher* phil = &phils[i];
		pthread_join(phil->thread, NULL);
	}

	printf("-- Finished --\n");
	for (int i = 0; i < PHILO; i++) {
		printf("Philosopher %d finished; ate total: %d\n", i, phils[i].foodAte);
	}

	return 0;
}
