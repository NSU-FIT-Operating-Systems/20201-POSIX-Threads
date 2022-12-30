#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <semaphore.h>
#define NUM_PRINTS 10
#define NUM_THREADS 2

sem_t * locks;

struct threadInput{
	char * str;
	size_t rank;
	size_t size;
};

void * printThread(void * ptr){
	struct threadInput * inp = (struct threadInput *)ptr;
	size_t total = inp->size;
	for(size_t i = 0; i < NUM_PRINTS; i++){
		sem_wait(&locks[inp->rank]);
		printf("%s\n", inp->str);
		sem_post(&locks[(inp->rank + 1) % total]);
	}
	pthread_exit(NULL);
}

int main(){
	locks = (sem_t*)calloc(NUM_THREADS, sizeof(sem_t));
	if(locks == NULL){
		printf("Failed to allocate memory\n");
		pthread_exit(NULL);
	}

	pthread_t ids[NUM_THREADS];
	struct threadInput * inputs = calloc(NUM_THREADS, sizeof(struct threadInput));
	if(inputs == NULL){
		printf("Failed to allocate inputs\n");
		free(locks);
		pthread_exit(NULL);
	}

	sem_init(locks, 0, 1);
	for(size_t i = 1; i < NUM_THREADS; i++){
		sem_init(locks + i, 0, 0);
	}

	char * inpStart = "This is thread ";

	for(size_t i = 0; i < NUM_THREADS; i++){
		char * inpstr = calloc(strlen(inpStart) + 2, sizeof(char));
		strcpy(inpstr, inpStart);
		inpstr[strlen(inpStart)] = 'A'+ (char)i;
		inpstr[strlen(inpStart) + 1] = '\0';

		inputs[i] = (struct threadInput) {inpstr, i, (size_t)NUM_THREADS};

		if(pthread_create(ids + i, NULL, &(printThread), inputs + i) != 0){
			printf("Failed to create thread\n");
			for(size_t idx = 0; idx < i; idx++){
				pthread_cancel(ids[idx]);
				pthread_join(ids[idx], NULL);
			}
			for(size_t idx = 0; idx < NUM_THREADS; idx++){
				sem_destroy(locks + idx);
			}
			free(locks);
			free(inputs);
			pthread_exit(NULL);
		}
	}
	for(size_t i = 0; i < NUM_THREADS; i++){
		pthread_join(ids[i], NULL);
	}
	for(size_t i = 0; i < NUM_THREADS; i++){
		sem_destroy(locks + i);
	}
	free(locks);
	free(inputs);
	pthread_exit(NULL);
}
