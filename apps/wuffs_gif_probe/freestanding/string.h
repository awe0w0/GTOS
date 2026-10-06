#ifndef GTOS_QUALIFICATION_STRING_H
#define GTOS_QUALIFICATION_STRING_H
#include <stddef.h>
void* memcpy(void* restrict,const void* restrict,size_t);
void* memmove(void*,const void*,size_t);
void* memset(void*,int,size_t);
int memcmp(const void*,const void*,size_t);
#endif
