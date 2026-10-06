#ifndef GTOS_QUALIFICATION_STDLIB_H
#define GTOS_QUALIFICATION_STDLIB_H
#include <stddef.h>
// Wuffs convenience allocators are unreferenced and removed by GC sections.
// These declarations are not implemented or callable services. Qualification
// fails if the final target retains any unresolved allocation symbol.
void* malloc(size_t);
void* calloc(size_t,size_t);
void* realloc(void*,size_t);
void free(void*);
#endif
