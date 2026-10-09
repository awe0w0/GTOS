#ifndef GTOS_STORAGE_LITTLEFS_PORT_H
#define GTOS_STORAGE_LITTLEFS_PORT_H
#include <common/types.h>
// Full upstream read/write algorithms, with static buffers and integer helpers.
#define LFS_NO_MALLOC
#define LFS_NO_DEBUG
#define LFS_NO_WARN
#define LFS_NO_ERROR
#define LFS_NO_INTRINSICS
#ifdef __cplusplus
extern "C" {
#endif
void gtos_lfs_assert_fail(const char* file,uint32_t line);
void* gtos_lfs_memcpy(void* to,const void* from,size_t size);
void* gtos_lfs_memset(void* to,int value,size_t size);
int gtos_lfs_memcmp(const void* a,const void* b,size_t size);
char* gtos_lfs_strchr(const char* text,int value);
char* gtos_lfs_strcpy(char* to,const char* from);
size_t gtos_lfs_strspn(const char* text,const char* set);
size_t gtos_lfs_strcspn(const char* text,const char* set);
#ifdef __cplusplus
}
#endif
#define LFS_ASSERT(test) ((test)?(void)0:gtos_lfs_assert_fail(__FILE__,__LINE__))
#define memcpy gtos_lfs_memcpy
#define memset gtos_lfs_memset
#define memcmp gtos_lfs_memcmp
#define strchr gtos_lfs_strchr
#define strcpy gtos_lfs_strcpy
#define strspn gtos_lfs_strspn
#define strcspn gtos_lfs_strcspn
#endif
