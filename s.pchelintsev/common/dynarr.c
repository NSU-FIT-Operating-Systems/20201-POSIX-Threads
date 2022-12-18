#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "dynarr.h"
#include "asalloc.h"

#ifndef M_LOG2E // micro$oft
#define M_LOG2E (double)1.44269504088896340736
#endif

#define MIN(a, b) ( ((a) < (b)) ? (a) : (b) )
#define MAX(a, b) ( ((a) > (b)) ? (a) : (b) )

static inline size_t ceilPO2(const size_t num) {
	// aka `log2(num + 1)`
	unsigned char needBits = log((double)(num + 1)) * M_LOG2E;
	return (size_t)(1 << (unsigned char)(needBits + 1));
}

static void ensureSpace(DynArray* mem, size_t sz, size_t datasz) {
	if (mem->fits < sz) {
		size_t newSize = ceilPO2(sz);

		void* newPtr = asrealloc(mem->ptr, newSize * datasz);

		mem->ptr = newPtr;
		mem->fits = newSize;
	}
}

DynArray* daNew(size_t size, void (*dtor)(void*)) {
	if (size == 0) {
		size = 64;
	}

	DynArray* ret = asmalloc(sizeof(DynArray)); {
		ret->sz = 0;
		ret->fits = size;
		ret->ptr = ascalloc(size * sizeof(void*), 1);
		ret->dtor = dtor;
	}

	return ret;
}

void daEnsure(DynArray* what, size_t ind) {
	ensureSpace(what, ind + 1, sizeof(void*));
}

void daSet(DynArray* to, size_t key, void* what) {
	ensureSpace(to, key + 1, sizeof(what));
	to->ptr[key] = what;
}

void daPush(DynArray* to, void* what) {
	ensureSpace(to, to->sz + 1, sizeof(what));

	to->ptr[to->sz] = what;
	to->sz++;
}

void* daGet(DynArray* where, size_t ind) {
	assert(where->sz > ind); // sz is +1
	return where->ptr[ind];
}

void* daPop(DynArray* what) {
	if (what->sz == 0) {
		return NULL;
	}

	what->sz--;

	void* ret = what->ptr[what->sz];
	what->ptr[what->sz] = NULL;

	return ret;
}

void daSwapPop(DynArray* what, size_t ind) {
	what->ptr[ind] = what->ptr[what->sz - 1];
	daPop(what);
}

void daEmpty(DynArray* what) {
	what->sz = 0;
}

void daResize(DynArray* what, size_t new) {

	if (new < what->sz && what->dtor != NULL) {
		for (size_t i = new; i < what->sz; i++) {
			if (what->ptr[i] != NULL) {
				what->dtor(what->ptr[i]);
			}
		}
	}

	void* newPtr = asrealloc(what->ptr, new * sizeof(void*));

	what->sz = MIN(what->sz, new);
	what->fits = new;

	what->ptr = newPtr;
}

size_t daSize(DynArray* what) {
	return what->sz;
}

size_t daFits(DynArray* what) {
	return what->fits;
}

void daFree(DynArray* what) {
	if (what->dtor) {
		for (size_t i = 0; i < what->sz; i++) {
			if (what->ptr[i] != NULL) {
				what->dtor(what->ptr[i]);
			}
		}
	}

	free(what->ptr);
	free(what);
}
