#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define MAX_LINES 100

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

int threadInitCount = 0;
int numLines = 0;

static long coef = 1000;

void * sleepSort(void * el){
	pthread_mutex_lock(&lock);
	char * str = (char*)el;

	threadInitCount++;
	while(threadInitCount < numLines){
		pthread_cond_wait(&cond, &lock);
	}
	pthread_mutex_unlock(&lock);

	usleep(coef * strlen(str));
	printf("%s", str);
	pthread_exit(0);
}

int main(){
	char * lines[MAX_LINES];
	size_t size = 0;
	char * line = NULL;
	size_t linelen;
	while((getline(&line, &linelen, stdin) > 1) && (size < MAX_LINES)){
		lines[size] = line;
		size++;
		line = NULL;
	}

	pthread_t threadIDs[MAX_LINES];
	numLines = size;
	for(size_t i = 0; i < size; i++){
		pthread_t id;
		if(pthread_create(&id, NULL, &sleepSort, lines[i]) != 0){
			for(size_t idx = 0; idx < i; idx++){
				pthread_cancel(threadIDs[idx]);
				pthread_join(threadIDs[idx], NULL);
			}
			printf("Insufficient resources to allocate enough threads (errno %d)\n", errno);
			for(size_t idx = 0; idx < size; idx++){
				free(lines[i]);
			}
			pthread_exit(0);
		}
		threadIDs[i] = id;
	}
	while(threadInitCount < numLines){
		usleep(100);
	}
	pthread_cond_broadcast(&cond);
	for(size_t i = 0; i < size; i++){
		pthread_join(threadIDs[i], NULL);
		free(lines[i]);
	}
	pthread_exit(0);
}
