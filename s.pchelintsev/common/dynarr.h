#ifndef idk_DynArr_IG
#define idk_DynArr_IG

#include <stdlib.h>
#include <stdlib.h>

typedef struct dynarr_t {
	void** ptr;
	size_t sz;
	size_t fits;

	void (*dtor)(void*);
} DynArray;


// dtor is optional, use NULL if not needed
DynArray* daNew(size_t size, void (*dtor)(void*));

void daPush(DynArray* to, void* what);
void daSet(DynArray* to, size_t key, void* what);

void* daPop(DynArray* what);
void daSwapPop(DynArray* what, size_t ind); // swaps the last element and [ind] then pops
void* daGet(DynArray*, size_t ind);

void daEmpty(DynArray* what); // idk why you'd need it lol
void daResize(DynArray* what, size_t new);

void daEnsure(DynArray* what, size_t sz);

size_t daSize(DynArray* what);
size_t daFits(DynArray* what);

void daFree(DynArray* what);
#endif
