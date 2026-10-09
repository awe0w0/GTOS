#ifndef GTOS_LFS_STDDEF_H
#define GTOS_LFS_STDDEF_H
#include <common/types.h>
#define NULL 0
#define offsetof(type,member) __builtin_offsetof(type,member)
#endif
