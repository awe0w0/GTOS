/*
 * The subset of <unistd.h> this project implements: read, write, lseek, close, dup, fsync and ftruncate. Other calls stay absent on
 * purpose — an #include that resolves to a declaration with no implementation behind it is exactly the kind of "present but not real"
 * this unit exists to avoid.
 */
#ifndef GTOS_UNISTD_H
#define GTOS_UNISTD_H

#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#ifdef __cplusplus
extern "C" {
#endif
ssize_t read(int fd, void *buffer, size_t bytes);
ssize_t write(int fd, const void *buffer, size_t bytes);
off_t lseek(int fd, off_t offset, int whence);
int close(int fd);
int dup(int fd);
int fsync(int fd);
int ftruncate(int fd, off_t length);
#ifdef __cplusplus
}
#endif

#endif /* GTOS_UNISTD_H */
