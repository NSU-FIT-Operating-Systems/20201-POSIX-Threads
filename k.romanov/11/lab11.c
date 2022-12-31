#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <stdatomic.h>
#define NUM_PRINTS 3
#define NUM_THREADS 5

pthread_mutex_t * locks;
atomic_size_t counter = 0;

struct threadInput{
	char * str;
	size_t rank;
	size_t size;
};

void * printThread(void * ptr){
	struct threadInput * inp = (struct threadInput *)ptr;
	size_t total = inp->size + 1;
	size_t init = (total - inp->rank) % total;
	if(pthread_mutex_lock(&locks[init]) != 0){
		printf("Invalid or uninitialized mutex");
		pthread_exit(NULL);
	}
	counter++;
	while(counter < inp->size){
		usleep(1000);
	}
	for(size_t i = 0; i < NUM_PRINTS; i++){
		if(pthread_mutex_lock(&locks[(i + 1 + init) % total]) != 0){
			printf("Invalid or uninitialized mutex");
			pthread_mutex_unlock(&locks[(i + init) % total]);
			pthread_exit(NULL);
		}
		printf("%s\n", inp->str);
		pthread_mutex_unlock(&locks[(i + init) % total]);
	}
	pthread_mutex_unlock(&locks[(init + NUM_PRINTS) % total]);
	pthread_exit(NULL);
}

int main(){
	locks = (pthread_mutex_t*)calloc(NUM_THREADS + 1, sizeof(pthread_mutex_t));
	if(locks == NULL){
		printf("Failed to allocate memory\n");
		pthread_exit(NULL);
	}

	pthread_mutexattr_t attr;
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);

	pthread_t ids[NUM_THREADS];
	struct threadInput * inputs = calloc(NUM_THREADS, sizeof(struct threadInput));
	if(inputs == NULL){
		printf("Failed to allocate inputs\n");
		free(locks);
		pthread_exit(NULL);
	}

	for(size_t i = 0; i < NUM_THREADS + 1; i++){
		if(pthread_mutex_init(locks + i, &attr)){
			printf("Failed to init mutex\n");
			for(size_t idx = 0; idx < i; idx++){
				pthread_mutex_destroy(locks + idx);
			}
			free(locks);
			free(inputs);
			pthread_exit(NULL);
		}
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
			for(size_t idx = 0; idx < NUM_THREADS + 1; idx++){
				pthread_mutex_destroy(locks + idx);
			}
			free(locks);
			free(inputs);
			pthread_exit(NULL);
		}
	}
	for(size_t i = 0; i < NUM_THREADS; i++){
		pthread_join(ids[i], NULL);
	}
	for(size_t i = 0; i < NUM_THREADS + 1; i++){
		pthread_mutex_destroy(locks + i);
	}
	free(locks);
	free(inputs);
	pthread_exit(NULL);
}
