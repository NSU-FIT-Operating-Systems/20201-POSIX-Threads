#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#define SUM_GRANULARITY 10000

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
size_t currBlock = 0;
int endFlag = 1;

void handler(int sig){
	endFlag = 0;
}

void * calcPi(void * inp){
	size_t rank = (size_t)inp;
	double * partialSum = (double*)malloc(sizeof(double));
	*partialSum = 0;
	size_t iterCounter = 0;

	while(endFlag != 0){
		pthread_mutex_lock(&lock);
		size_t curr = currBlock++;
		pthread_mutex_unlock(&lock);
		size_t offset = 2 * curr * SUM_GRANULARITY;
		for(size_t i = 0; i < SUM_GRANULARITY; i++){
			*partialSum += 1.0/((double)(2 * (2 * i + offset) + 1.0));
			*partialSum -= 1.0/((double)(2 * (2 * i + offset + 1) + 1.0));
		}
	}
	pthread_exit(partialSum);
}

int main(int argc, char ** argv){
	if(argc < 2){
		printf("Provide the number of threads to run!\n");
		pthread_exit(NULL);
	}
	size_t expThreads = atoi(argv[1]);
	if(expThreads < 1){
		printf("Invalid integer input: %s\n", argv[1]);
		pthread_exit(NULL);
	}
	
	struct sigaction sa;
	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if(sigaction(SIGINT, &sa, NULL) < 0){
		printf("Could not install signal handler\n");
		pthread_exit(NULL);
	}

	int threadFlag = 1;
	size_t count = 0;
	pthread_t * threadIDs = (pthread_t*)malloc(expThreads * sizeof(pthread_t));
	while((count < expThreads) && threadFlag){
		pthread_t id;
		if(pthread_create(&id, NULL, &calcPi, (void *)count)){
			printf("Could not create more than %ld threads, running anyway\n", count);
			threadFlag = 0;
		}
		else{
			threadIDs[count] = id;
			count++;
		}
	}
	if(count == 0){
		printf("Thread count is zero, cannot calculate\n");
		pthread_exit(NULL);
	}

	double accum = 0;
	for(size_t i = 0; i < count; i++){
		void * val;
		pthread_join(threadIDs[i], &val);
		accum += *(double*)val;
		free(val);
	}
	printf("PI: %.17g\n", accum * 4.0);
	free(threadIDs);
	pthread_exit(NULL);
}
