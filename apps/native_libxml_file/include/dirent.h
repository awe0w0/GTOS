/* Native directory streams over the owner-scoped GTOS file service.
 * This is a private layout, not the Linux dirent ABI. The store supplies names
 * and types but no inode identity, so d_ino is zero. There is no seekdir/telldir
 * interface: the underlying directory cursor can only be read or rewound.
 */
#ifndef GTOS_DIRENT_H
#define GTOS_DIRENT_H

#include <stdint.h>

#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8

struct dirent {
  uint64_t d_ino;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[256];
};

typedef struct GtosDirectoryStream DIR;

#ifdef __cplusplus
extern "C" {
#endif
DIR *opendir(const char *path);
struct dirent *readdir(DIR *directory);
void rewinddir(DIR *directory);
int closedir(DIR *directory);
int dirfd(DIR *directory);
#ifdef __cplusplus
}
#endif

#endif
