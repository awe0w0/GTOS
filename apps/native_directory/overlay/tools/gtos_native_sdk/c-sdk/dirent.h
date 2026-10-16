#ifndef GTOS_NATIVE_DIRENT_H
#define GTOS_NATIVE_DIRENT_H
#include "__llvm-libc-common.h"
#if !defined(__GTOS__) || !defined(__i386__) || defined(__linux__)
#error GTOS directory declarations require the native IA32 ABI
#endif

// Only name and type are supplied by the GTOS directory ABI. There is no
// fabricated inode, offset cookie or promise of a Linux struct layout.
typedef struct __gtos_directory DIR;
struct dirent {
  unsigned char d_type;
  char d_name[256];
};
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12

__BEGIN_C_DECLS
DIR *opendir(const char *) __NOEXCEPT;
struct dirent *readdir(DIR *) __NOEXCEPT;
int closedir(DIR *) __NOEXCEPT;
void rewinddir(DIR *) __NOEXCEPT;
int dirfd(DIR *) __NOEXCEPT;
__END_C_DECLS
#endif
