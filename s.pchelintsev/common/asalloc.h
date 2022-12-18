#ifndef AssertAlloc_IG
#define AssertAlloc_IG

#include <stdlib.h>

void* asmalloc(size_t sz);
void* ascalloc(size_t amt, size_t sz);
void* asrealloc(void* ptr, size_t sz);

#endif
