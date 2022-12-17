#ifndef htDefined
#define htDefined

#include <stddef.h>
#include <stdint.h>

// structs:
struct htElem {
	void* data;

	char* key;
	size_t keySize;

	uint32_t hash1;
	uint32_t hash2;
	char taken;
};

typedef struct htElem htElem_t;

struct htTable_t {
	size_t curCap;
	size_t curStored;

	htElem_t** table;
	void (*dtor)(void*);
};

typedef struct htTable_t htTable;


// functions:
htTable* newHashtable(size_t cap, void dtor(void*));
void htInsert(htTable*, void* key, size_t keyLen, void* ptrData);
void* htGet(const htTable*, const void* key, const size_t keyLen);
void htFree(void* what);

#endif
