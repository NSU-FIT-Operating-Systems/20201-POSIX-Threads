/* 
 * File:   din_phil.c
 * Author: nd159473 (Nickolay Dalmatov, Sun Microsystems)
 * adapted from http://developers.sun.com/sunstudio/downloads/ssx/tha/tha_using_deadlock.html
 *
 * Created on January 1, 1970, 9:53 AM
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include <errno.h>

#define PHILO 5
#define DELAY 30000
#define FOOD 50

#define THINKTIME 10000

/*------------------------------------------*/
int eaten[PHILO] = {0};
int though[PHILO] = {0};
/*------------------------------------------*/

pthread_mutex_t forks[PHILO];
pthread_t phils[PHILO];
void *philosopher (void *id);
int food_on_table ();
void get_fork (int, int, char *);
void down_forks (int, int);
pthread_mutex_t foodlock;

int try_get_fork(int, int, char *);
void down_fork(int);

int sleep_seconds = 0;

int
main (int argn,
      char **argv)
{
  int i;

  if (argn == 2)
    sleep_seconds = atoi (argv[1]);

  pthread_mutex_init (&foodlock, NULL);
  for (i = 0; i < PHILO; i++)
    pthread_mutex_init (&forks[i], NULL);
  for (i = 0; i < PHILO; i++)
    pthread_create (&phils[i], NULL, philosopher, (void *)i);
  for (i = 0; i < PHILO; i++)
    pthread_join (phils[i], NULL);
  return 0;
}

void *
philosopher (void *num)
{
  int id;
  int left_fork, right_fork, f;

  id = (int)num;
  printf ("Philosopher %d sitting down to dinner.\n", id);
  left_fork = id;
  right_fork = id + 1;
 
  /* Wrap around the forks. */
  if (right_fork == PHILO)
    right_fork = 0;
 
  while (f = food_on_table ()) {

    /* Thanks to philosophers #1 who would like to 
     * take a nap before picking up the forks, the other
     * philosophers may be able to eat their dishes and 
     * not deadlock.
     */
    if (id == 1)
      sleep (sleep_seconds);

    printf ("Philosopher %d: get dish %d.\n", id, f);
    while(1) {
      /*thinking -------------------------------------*/
      long rand = random();
      rand = (rand % THINKTIME);
      usleep(rand);
      though[id] += 1;
      /*----------------------------------------------*/

      /*printf ("Philosopher %d: get dish %d.\n", id, f); */

      int resl = try_get_fork (id, left_fork, "left");
      if (0 != resl) {continue;}

      int resr = try_get_fork (id, right_fork, "right");
      if (0 != resr) {down_fork(left_fork); continue;}

      break;
    }

    printf ("Philosopher %d: eating.\n", id);
    usleep (DELAY * (FOOD - f + 1));
    down_forks (left_fork, right_fork);
    eaten[id] += 1;
  }
  printf ("Philosopher %d is done eating.\n", id);
  printf ("---Philosopher %d ate [%d] and though {%d}\n", id, eaten[id], though[id]);
  return (NULL);
}

int
food_on_table ()
{
  static int food = FOOD;
  int myfood;

  pthread_mutex_lock (&foodlock);
  if (food > 0) {
    food--;
  }
  myfood = food;
  pthread_mutex_unlock (&foodlock);
  return myfood;
}

void
get_fork (int phil,
          int fork,
          char *hand)
{
  pthread_mutex_lock (&forks[fork]);
  /*printf ("Philosopher %d: got %s fork %d\n", phil, hand, fork);*/
}

int try_get_fork(int phil, int fork, char *hand) {
  int res = pthread_mutex_trylock (&forks[fork]);
  if (res == EBUSY) {
    /*printf ("Philosopher %d: tried to get %s fork %d, but it was taken already\n", phil, hand, fork); */
    return 1;
  }
  printf ("Philosopher %d: got %s fork %d\n", phil, hand, fork);
  return 0;
}

void down_fork(int f) {
  pthread_mutex_unlock(&forks[f]);
}

void
down_forks (int f1,
            int f2)
{
  pthread_mutex_unlock (&forks[f1]);
  pthread_mutex_unlock (&forks[f2]);
}
