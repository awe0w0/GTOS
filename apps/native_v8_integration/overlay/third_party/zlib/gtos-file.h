#ifndef GTOS_ZLIB_NATIVE_FILE_H
#define GTOS_ZLIB_NATIVE_FILE_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <process/file_abi.h>

#if !defined(__GTOS__) || !defined(__i386__) || defined(__linux__)
#error This zlib backend requires the independent GTOS IA32 file ABI
#endif

/* Local zlib flags map directly to GTOS flags. These are native owner handles,
   not Linux descriptors. FileStore never issues a handle above INT32_MAX. */
#define O_RDONLY ((int)GTOS_FILE_READ)
#define O_WRONLY ((int)GTOS_FILE_WRITE)
#define O_CREAT ((int)GTOS_FILE_CREATE)
#define O_EXCL ((int)GTOS_FILE_EXCLUSIVE)
#define O_TRUNC ((int)GTOS_FILE_TRUNCATE)
#define O_APPEND ((int)GTOS_FILE_APPEND)

#ifdef __cplusplus
static_assert(sizeof(void *) == 4 && sizeof(unsigned) == 4 && sizeof(int) == 4,
              "GTOS file ABI widths");
#else
_Static_assert(sizeof(void *) == 4 && sizeof(unsigned) == 4 && sizeof(int) == 4,
               "GTOS file ABI widths");
#endif

static inline int gtos_zlib_call(unsigned operation, const void *request,
                                 unsigned bytes) {
    unsigned result = operation;
    __asm__ volatile("int $0x80" : "+a"(result)
                     : "b"((uintptr_t)request), "c"(bytes) : "memory", "cc");
    return (int)result;
}

static inline int gtos_zlib_error(int result) {
    switch (result) {
    case -2: errno = ENOENT; break;
    case -5: errno = EIO; break;
    case -7: errno = E2BIG; break;
    case -9: errno = EBADF; break;
    case -12: errno = ENOMEM; break;
    case -14: errno = EFAULT; break;
    case -16: errno = EBUSY; break;
    case -17: errno = EEXIST; break;
    case -19: errno = ENODEV; break;
    case -20: errno = ENOTDIR; break;
    case -21: errno = EISDIR; break;
    case -22: errno = EINVAL; break;
    case -24: errno = EMFILE; break;
    case -27: errno = EFBIG; break;
    case -28: errno = ENOSPC; break;
    case -30: errno = EROFS; break;
    case -36: errno = ENAMETOOLONG; break;
    case -38: errno = ENOSYS; break;
    case -39: errno = ENOTEMPTY; break;
    case -75: errno = EOVERFLOW; break;
    default: errno = EIO; break;
    }
    return -1;
}

static inline int gtos_zlib_open(const char *path, int flags,
                                int permissions) {
    unsigned length;
    GtosFileOpenRequest request;
    int result;
    (void)permissions; /* GTOS FileStore has no POSIX permission mode. */
    if (path == NULL) { errno = EINVAL; return -1; }
    for (length = 0; length < GTOS_FILE_PATH_LIMIT; ++length) {
        if (path[length] == '\0') break;
    }
    if (length == GTOS_FILE_PATH_LIMIT) { errno = ENAMETOOLONG; return -1; }
    request.version = GTOS_FILE_ABI_VERSION;
    request.path = (uintptr_t)path;
    request.path_bytes = length + 1;
    request.flags = (unsigned)flags;
    result = gtos_zlib_call(GTOS_SYS_FILE_OPEN, &request, sizeof(request));
    return result < 0 ? gtos_zlib_error(result) : result;
}

static inline int gtos_zlib_transfer(int handle, void *buffer, unsigned bytes,
                                    unsigned operation) {
    GtosFileTransferRequest request;
    int result;
    if (handle <= 0) { errno = EBADF; return -1; }
    if (bytes > UINT32_MAX - (uintptr_t)buffer) { errno = EOVERFLOW; return -1; }
    request.version = GTOS_FILE_ABI_VERSION;
    request.handle = (unsigned)handle;
    request.buffer = (uintptr_t)buffer;
    /* zlib already handles partial transfers. Each native request is bounded
       and its complete requested page range is checked by the kernel. */
    request.bytes = bytes > GTOS_FILE_TRANSFER_LIMIT ? GTOS_FILE_TRANSFER_LIMIT : bytes;
    result = gtos_zlib_call(operation, &request, sizeof(request));
    return result < 0 ? gtos_zlib_error(result) : result;
}

static inline int gtos_zlib_read(int handle, void *buffer, unsigned bytes) {
    return gtos_zlib_transfer(handle, buffer, bytes, GTOS_SYS_FILE_READ);
}

static inline int gtos_zlib_write(int handle, const void *buffer, unsigned bytes) {
    return gtos_zlib_transfer(handle, (void *)buffer, bytes, GTOS_SYS_FILE_WRITE);
}

static inline int64_t gtos_zlib_seek(int handle, int64_t offset, int whence) {
    GtosFileSeekRequest request;
    uint64_t bits = (uint64_t)offset;
    int result;
    if (handle <= 0) { errno = EBADF; return -1; }
    request.version = GTOS_FILE_ABI_VERSION;
    request.handle = (unsigned)handle;
    request.offset_low = (unsigned)bits;
    request.offset_high = (unsigned)(bits >> 32);
    request.whence = (unsigned)whence;
    result = gtos_zlib_call(GTOS_SYS_FILE_SEEK, &request, sizeof(request));
    return result < 0 ? gtos_zlib_error(result) : result;
}

static inline int gtos_zlib_close(int handle) {
    GtosFileControlRequest request = {GTOS_FILE_ABI_VERSION, (unsigned)handle};
    int result;
    if (handle <= 0) { errno = EBADF; return -1; }
    result = gtos_zlib_call(GTOS_SYS_FILE_CLOSE, &request, sizeof(request));
    return result < 0 ? gtos_zlib_error(result) : result;
}

#endif
