#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#define FOOD 50
#define DELAY 500
#define PHILO 5
#define ERROR_BUFFER_LEN 120


pthread_cond_t all_forks_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t all_forks_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t food_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t forks_mutex[PHILO] = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
                                      PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
                                      PTHREAD_MUTEX_INITIALIZER};
pthread_t threads[PHILO];

void print_error(const char *message, int code) {
    char buffer[ERROR_BUFFER_LEN];

        strcpy(buffer, "Can't generate error");
    write(STDERR_FILENO, buffer, strlen(buffer));
}

void cancel_threads(int id) {
    for (int i = 0; i < PHILO; i++) {
        if (id == i) continue;
        pthread_cancel(threads[i]);
    }
    pthread_cancel(threads[id]);
}

void lock_mutex(pthread_mutex_t *mutex, int id) {
    int error_code = pthread_mutex_lock(mutex);
    if (error_code != 0) {
        print_error("Can't lock mutex", error_code);
        cancel_threads(id);
    }
}

bool is_mutex_locked(pthread_mutex_t *mutex, int id) {
    int error_code = pthread_mutex_trylock(mutex);
    if (error_code != 0) {
        if (error_code == EBUSY) {
            return true;
        }
        print_error("Can't trylock thread", error_code);
        cancel_threads(id);
    }
    return false;
}

void unlock_mutex(pthread_mutex_t *mutex, int id) {
    int error_code = pthread_mutex_unlock(mutex);
    if (error_code != 0) {
        print_error("Can't unlock mutex", error_code);
        cancel_threads(id);
    }
}

void wait_cond(pthread_cond_t *cond, pthread_mutex_t *mutex, int id) {
    int error_code = pthread_cond_wait(cond, mutex);
    if (error_code != 0) {
        print_error("Can't wait on cond variable", error_code);
        cancel_threads(id);
    }
}

void broadcast_cond(pthread_cond_t *cond, int id) {
    int error_code = pthread_cond_signal(cond);
    if (error_code != 0) {
        print_error("Can't broadcast cond variable", error_code);
        cancel_threads(id);
    }
}

int get_food(int id) {
    static int total_food = FOOD;
    int my_food;

    lock_mutex(&food_mutex, id);
    my_food = total_food;
    if (total_food > 0) {
        total_food--;
    }
    unlock_mutex(&food_mutex, id);

    return my_food;
}

void pick_forks_up(int phil, int left_fork, int right_fork) {
    lock_mutex(&all_forks_mutex, phil);

    while (true) {
        bool is_right_locked = is_mutex_locked(&forks_mutex[right_fork], phil);
        if (!is_right_locked) {
            bool is_left_locked = is_mutex_locked(&forks_mutex[left_fork], phil);
            if (!is_left_locked) {
                break;
            }
            unlock_mutex(&forks_mutex[right_fork], phil);
        }
        wait_cond(&all_forks_cond, &all_forks_mutex, phil);
    }

    unlock_mutex(&all_forks_mutex, phil);
    printf("Philosopher %d: got forks %d and %d.\n", phil + 1, left_fork + 1, right_fork + 1);
}

void put_forks_down(int phil, int left_fork, int right_fork) {
    lock_mutex(&all_forks_mutex, phil);

    unlock_mutex(&forks_mutex[left_fork], phil);
    unlock_mutex(&forks_mutex[right_fork], phil);

    broadcast_cond(&all_forks_cond, phil);

    unlock_mutex(&all_forks_mutex, phil);
}

void *philosopher(void *param) {


    int *tmp_arr = (int *) param;
    int id = tmp_arr[0];
    int right_fork = id;
    int left_fork = id + 1;

    if (left_fork == PHILO) {
        left_fork = right_fork;
        right_fork = 0;
    }

    printf("Philosopher %d sat to the table.\n", id + 1);

    int food;
    int total = 0;
    while ((food = get_food(id)) > 0) {
        total++;
        printf("Philosopher %d: gets food %d.\n", id + 1, food);

        pick_forks_up(id, left_fork, right_fork);

        printf("Philosopher %d: eats.\n", id + 1);
        usleep(DELAY * (FOOD - food + 1));

        put_forks_down(id, left_fork, right_fork);

    }

    printf("Philosopher %d is done eating. Ate %d out of %d meals\n", id + 1, total, FOOD);
    return param;
}

void cleanup() {
    pthread_cond_destroy(&all_forks_cond);
    pthread_mutex_destroy(&all_forks_mutex);
    pthread_mutex_destroy(&food_mutex);
    for (int i = 0; i < PHILO; i++) {
        pthread_mutex_destroy(&forks_mutex[i]);
    }
}

int main() {
    int error_code;
    for (int i = 0; i < PHILO; i++) {
        int *id = (int*)malloc(sizeof(int));
        id[0] = i;
        error_code = pthread_create(&threads[i], NULL, philosopher, (void *) id);
        if (error_code != 0) {
            print_error("Can't create thread", error_code);
        }
    }
    atexit(cleanup);
    pthread_exit(NULL);
}
