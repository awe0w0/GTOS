/*
 * The POSIX file subset libxml2's xmlIO.c (and the Chromium consumers that read XML from a file) actually names.
 *
 * This is a private SDK header, not a POSIX claim: it declares the calls this project implements over the GTOS file ABI, so a libxml2
 * source that includes <fcntl.h> compiles against what GTOS can really do instead of against a Linux header that would promise more.
 * The values are the i386 Linux ones because that is what callers pass (xmlIO.c passes O_RDONLY/O_WRONLY/O_CREAT|O_TRUNC and 0666);
 * posix_file.cpp translates them to GTOS_FILE_* explicitly, so a mismatch would be a build error rather than a silent wrong mode.
 */
#ifndef GTOS_FCNTL_H
#define GTOS_FCNTL_H

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_EXCL 0200
#define O_TRUNC 01000
#define O_APPEND 02000

#ifdef __cplusplus
extern "C" {
#endif
int open(const char *path, int flags, ...);
#ifdef __cplusplus
}
#endif

#endif /* GTOS_FCNTL_H */
