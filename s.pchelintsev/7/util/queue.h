#ifndef Queue_IG
#define Queue_IG

#include <stdlib.h>
#include "dynarr.h"

typedef struct queue_t /*pie*/ {
	size_t pushCursor;
	size_t popCursor;

	size_t amtValues;		// how many values we can pop

	size_t slotsAvailable;	// amt of values we can overwrite before the popCursor
	size_t needShift;		// how many values after rightEdge we need to shift after pushCursor
	size_t rightEdge;		// place at which popCursor will go back to the start

	DynArray* arr;
} Queue;


void* qPop(Queue* queue);

void qAdd(Queue* queue, void* item);

char qEmpty(Queue* queue);

size_t qSize(Queue* queue);

Queue* newQueue(size_t sz);
void qFree(Queue* queue);
#endif
