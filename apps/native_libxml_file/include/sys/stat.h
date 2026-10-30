/*
 * <sys/stat.h> with the fields the GTOS file service can actually fill: mode (regular file or directory, from the ABI's type field) and
 * size (from the ABI's byte count). The remaining fields are zeroed rather than invented, and struct stat is deliberately not the Linux
 * layout: nothing here claims to be a Linux runtime, and a caller that needs st_ino or st_uid has no GTOS answer to get.
 */
#ifndef GTOS_SYS_STAT_H
#define GTOS_SYS_STAT_H

#include <sys/types.h>

struct stat {
  unsigned st_dev;
  unsigned st_ino;
  unsigned short st_mode;
  unsigned short st_nlink;
  unsigned st_uid;
  unsigned st_gid;
  unsigned st_rdev;
  long long st_size;
  long st_blksize;
  long long st_blocks;
  long st_atime;
  long st_mtime;
  long st_ctime;
};

#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)

#ifdef __cplusplus
extern "C" {
#endif
int stat(const char *path, struct stat *buffer);
#ifdef __cplusplus
}
#endif

#endif /* GTOS_SYS_STAT_H */
