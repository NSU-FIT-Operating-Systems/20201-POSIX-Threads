#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "idk.h"

Queue* newQueue(size_t sz) {
	Queue* queue = malloc(sizeof(Queue));
	queue->popCursor = 0;
	queue->pushCursor = 0;
	queue->slotsAvailable = 0;
	queue->needShift = 0;
	queue->rightEdge = 0;
	queue->amtValues = 0;

	queue->arr = daNew(sz, NULL);

	return queue;
}

static char atEnd(Queue* queue) {
	return queue->rightEdge <= queue->popCursor;
}

void qAdd(Queue* queue, void* item) {
	if (queue->slotsAvailable > 0 && !queue->needShift) {
		// we're pushing behind pop
		daSet(queue->arr, queue->pushCursor, item);
		queue->pushCursor++;
		queue->slotsAvailable--;
	} else {
		// we're pushing in front of pop, so we need
		// to push at the end of the array
		daPush(queue->arr, item);

		if (queue->popCursor > 0) {
			queue->needShift++;
		}
	}

	queue->amtValues++;
}

void* qPop(Queue* queue) {
	if (queue->amtValues == 0) {
		return NULL;
	}

	if (atEnd(queue)) {
		// we're at the end of the queue; let's start from the bottom
		size_t cursor = queue->pushCursor;

		for (size_t i = 0; i < queue->needShift; i++) {
			size_t arrIndex = queue->rightEdge + i;

			queue->arr->ptr[cursor] = queue->arr->ptr[arrIndex];
			cursor++;
		}

		queue->slotsAvailable = 0;
		queue->popCursor = 0;
		queue->rightEdge = 0;
		queue->pushCursor = 0;
		queue->needShift = 0;
	}

	void* item = queue->arr->ptr[queue->popCursor];
	queue->arr->ptr[queue->popCursor] = NULL;

	queue->slotsAvailable++;
	queue->popCursor++;

	if (queue->rightEdge == 0) {
		queue->rightEdge = queue->amtValues;
	}

	queue->amtValues--;

	return item;
}

void qFree(Queue* queue) {
	daFree(queue->arr);
	free(queue);
}

char qEmpty(Queue* queue) {
	return queue->amtValues == 0;
}

size_t qSize(Queue* queue) {
	return queue->amtValues;
}

/*
int main() {
	Queue* queue = newQueue(8);

	for (int i=0; i < 8; i++) {
		printf("queueing %d\n", i);
		qAdd( queue, (void*)i );
	}

	for (int i=0; i < 4; i++) {
		void* res = qPop(queue);

		printf("popping #%d: %d\n", i, (int)res);
	}

	for (int i=8; i < 16; i++) {
		printf("queueing %d\n", i);
		qAdd( queue, (void*)i );
	}

	for (int i=4; i < 8; i++) {
		void* res = qPop(queue);

		printf("popping #%d: %d\n", i, (int)res);
	}

	for (int i=16; i < 64; i++) {
		printf("queueing %d\n", i);
		qAdd( queue, (void*)i );
	}

	for (int i=8; i < 64; i++) {
		void* res = qPop(queue);

		printf("popping #%d: %d\n", i, (int)res);
	}

	printf("size: %d\n", queue->arr->fits);
	qFree(queue);
}
*/
