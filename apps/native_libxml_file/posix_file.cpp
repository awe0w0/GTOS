/*
 * The POSIX file calls of the private SDK subset, implemented as thin, honest translations onto the GTOS file ABI (include/process/file_abi.h).
 *
 * What this is: the missing half of `#include <fcntl.h>` / `<unistd.h>` / `<sys/stat.h>` for the GTOS target. Without it xmlIO.c cannot
 * even compile (r20's measurement: "fatal error: 'fcntl.h' file not found", 25 of 26 libxml sources OK). With it, a libxml2 source that
 * reads a file is talking to the same file service every other GTOS consumer uses — the same open/read/seek/dup/close the native FILE
 * layer calls through LLVM libc's platform_file.cpp — rather than to a second, parallel file layer.
 *
 * What this is not: a POSIX runtime. There is no permission model (the third argument of open() is accepted and ignored), no fstat (the
 * ABI stats a path, not a handle), no isatty, no O_NONBLOCK, no fd inheritance beyond the ABI's own owner-scoped handles. Each of those
 * absences is visible to the caller as a link error or a refused flag, not as a declaration that quietly resolves to nothing.
 *
 * Handle space: a returned fd is the ABI's own handle, so it composes with the native FILE layer (fileno() returns the same number) and
 * dup() returns a second handle over the same open resource — the ABI documents that the offset and directory cursor are shared and that
 * closing one duplicate invalidates only that handle. No fd table is kept here, because inventing a second numbering over the ABI's
 * handles would break exactly that composition.
 */
#include <process/file_abi.h>

#include <errno.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static_assert(sizeof(void *) == 4 && sizeof(unsigned) == 4, "the GTOS file ABI is a 32-bit interface");

// The pinned LLVM libc header set carries the common errno names only: its non-Linux branch defines a small generic subset, and the file ABI
// has errors outside that subset. These are the standard numbers, which are the ones GTOS's ABI mirrors (its negative codes are the errno
// values), so every caller comparing against the same headers sees the same number.
#ifndef ENAMETOOLONG
#define ENAMETOOLONG 36
#endif
#ifndef ENOSYS
#define ENOSYS 38
#endif
#ifndef ENOTEMPTY
#define ENOTEMPTY 39
#endif
#ifndef EOVERFLOW
#define EOVERFLOW 75
#endif
#ifndef ENOTSUP
#define ENOTSUP 95
#endif

namespace {

int call(unsigned operation, const void *request, unsigned bytes) {
  unsigned result = operation;
  asm volatile("int $0x80" : "+a"(result) : "b"(reinterpret_cast<unsigned>(request)), "c"(bytes) : "memory", "cc");
  return static_cast<int>(result);
}

int translate(int result) {
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

int fail_with(int error) {
  errno = error;
  return -1;
}

// Everything this layer understands. A bit outside it is refused: dropping an unknown open flag turns a caller's O_DIRECTORY or O_SYNC into
// a silently ordinary open, and the caller then reports success for a contract nobody kept.
const int kAccessMask = 3;
const int kKnownFlags = O_CREAT | O_EXCL | O_TRUNC | O_APPEND;

int open_flags(int flags) {
  unsigned result;
  switch (flags & kAccessMask) {
  case O_RDONLY: result = GTOS_FILE_READ; break;
  case O_WRONLY: result = GTOS_FILE_WRITE; break;
  case O_RDWR: result = GTOS_FILE_READ_WRITE; break;
  default: errno = EINVAL; return -1;
  }
  if (flags & ~(kAccessMask | kKnownFlags)) {
    errno = EINVAL;
    return -1;
  }
  if (flags & O_CREAT) result |= GTOS_FILE_CREATE;
  if (flags & O_EXCL) result |= GTOS_FILE_EXCLUSIVE;
  if (flags & O_TRUNC) result |= GTOS_FILE_TRUNCATE;
  if (flags & O_APPEND) result |= GTOS_FILE_APPEND;
  return static_cast<int>(result);
}

// The ABI takes a NUL-terminated path with its final NUL counted (GTOS_FILE_PATH_LIMIT bytes at most) and rejects a trailing empty name.
// The scan is bounded rather than strlen(): a path that has no terminator inside the ABI's own limit is ENAMETOOLONG, and reading further
// would be reading whatever the caller happens to have after the buffer.
int path_bytes(const char *path, unsigned *bytes) {
  if (path == 0) {
    errno = EINVAL;
    return -1;
  }
  unsigned length = 0;
  while (length < GTOS_FILE_PATH_LIMIT && path[length] != 0) ++length;
  if (length == GTOS_FILE_PATH_LIMIT) {
    errno = ENAMETOOLONG;
    return -1;
  }
  if (length == 0) {
    errno = ENOENT;
    return -1;
  }
  *bytes = length + 1;
  return 0;
}

} // namespace

extern "C" int open(const char *path, int flags, ...) {
  unsigned bytes = 0;
  if (path_bytes(path, &bytes) != 0) return -1;
  const int translated = open_flags(flags);
  if (translated < 0) return -1;
  const GtosFileOpenRequest request{GTOS_FILE_ABI_VERSION, reinterpret_cast<unsigned>(path), bytes, static_cast<unsigned>(translated)};
  const int handle = call(GTOS_SYS_FILE_OPEN, &request, sizeof(request));
  if (handle < 0) return translate(handle);
  return handle;
}

extern "C" int close(int fd) {
  if (fd <= 0) return fail_with(EBADF);
  const GtosFileControlRequest request{GTOS_FILE_ABI_VERSION, static_cast<unsigned>(fd)};
  const int result = call(GTOS_SYS_FILE_CLOSE, &request, sizeof(request));
  return result < 0 ? translate(result) : 0;
}

extern "C" int dup(int fd) {
  if (fd <= 0) return fail_with(EBADF);
  const GtosFileControlRequest request{GTOS_FILE_ABI_VERSION, static_cast<unsigned>(fd)};
  const int handle = call(GTOS_SYS_FILE_DUP, &request, sizeof(request));
  if (handle < 0) return translate(handle);
  return handle;
}

extern "C" ssize_t read(int fd, void *buffer, size_t bytes) {
  if (fd <= 0) return fail_with(EBADF);
  if (buffer == 0 && bytes != 0) return fail_with(EFAULT);
  // One request carries at most GTOS_FILE_TRANSFER_LIMIT bytes; a short read is a legal POSIX result and the caller's loop decides when to
  // ask again. Splitting a single read() into several transfers would be a lie about where the file offset stands after a short read.
  const size_t counted = bytes > GTOS_FILE_TRANSFER_LIMIT ? GTOS_FILE_TRANSFER_LIMIT : bytes;
  const GtosFileTransferRequest request{GTOS_FILE_ABI_VERSION, static_cast<unsigned>(fd), reinterpret_cast<unsigned>(buffer),
                                        static_cast<unsigned>(counted)};
  const int result = call(GTOS_SYS_FILE_READ, &request, sizeof(request));
  return result < 0 ? translate(result) : static_cast<ssize_t>(result);
}

extern "C" ssize_t write(int fd, const void *buffer, size_t bytes) {
  if (fd <= 0) return fail_with(EBADF);
  if (buffer == 0 && bytes != 0) return fail_with(EFAULT);
  // write() reports how many bytes were stored, so the loop is inside this call: partial progress is reported as a partial result, and an
  // error after some bytes keeps the count instead of losing bytes the file already holds.
  size_t done = 0;
  while (done < bytes) {
    const size_t left = bytes - done;
    const size_t counted = left > GTOS_FILE_TRANSFER_LIMIT ? GTOS_FILE_TRANSFER_LIMIT : left;
    const GtosFileTransferRequest request{GTOS_FILE_ABI_VERSION, static_cast<unsigned>(fd),
                                          reinterpret_cast<unsigned>(static_cast<const char *>(buffer) + done),
                                          static_cast<unsigned>(counted)};
    const int result = call(GTOS_SYS_FILE_WRITE, &request, sizeof(request));
    if (result < 0) return done == 0 ? translate(result) : static_cast<ssize_t>(done);
    done += static_cast<size_t>(result);
    if (static_cast<size_t>(result) < counted) break;
  }
  return static_cast<ssize_t>(done);
}

extern "C" off_t lseek(int fd, off_t offset, int whence) {
  if (fd <= 0) return fail_with(EBADF);
  if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) return fail_with(EINVAL);
  const long long wanted = static_cast<long long>(offset);
  const GtosFileSeekRequest request{GTOS_FILE_ABI_VERSION, static_cast<unsigned>(fd), static_cast<unsigned>(wanted),
                                    static_cast<unsigned>(wanted >> 32), static_cast<unsigned>(whence)};
  const int result = call(GTOS_SYS_FILE_SEEK, &request, sizeof(request));
  if (result < 0) return translate(result);
  return static_cast<off_t>(result);
}

extern "C" int stat(const char *path, struct stat *buffer) {
  unsigned bytes = 0;
  if (path_bytes(path, &bytes) != 0) return -1;
  if (buffer == 0) return fail_with(EINVAL);
  GtosFileInfo info{};
  const GtosFileStatRequest request{GTOS_FILE_ABI_VERSION, reinterpret_cast<unsigned>(path), bytes,
                                    reinterpret_cast<unsigned>(&info), sizeof(info)};
  const int result = call(GTOS_SYS_FILE_STAT, &request, sizeof(request));
  if (result < 0) return translate(result);
  memset(buffer, 0, sizeof(*buffer));
  buffer->st_mode = info.type == GTOS_FILE_TYPE_DIRECTORY ? S_IFDIR : S_IFREG;
  buffer->st_size = static_cast<long long>(info.bytes);
  return 0;
}
