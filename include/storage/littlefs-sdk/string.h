#ifndef GTOS_LFS_STRING_H
#define GTOS_LFS_STRING_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* memcpy(void* to,const void* from,size_t size);
void* memset(void* to,int value,size_t size);
int memcmp(const void* a,const void* b,size_t size);
char* strchr(const char* text,int value);
char* strcpy(char* to,const char* from);
size_t strspn(const char* text,const char* set);
size_t strcspn(const char* text,const char* set);
#ifdef __cplusplus
}
#endif
#endif
