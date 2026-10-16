#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <process/file_abi.h>

#if !defined(__GTOS__) || !defined(__i386__) || defined(__linux__)
#error This provider requires the native GTOS IA32 ABI
#endif
static_assert(sizeof(void *) == 4 && sizeof(int) == 4, "Native directory ABI");

// A stream owns one actual directory handle and its own readdir buffer.
// As with POSIX readdir, callers serialize operations on the same DIR object.
// Separate streams share no user-space state; the kernel serializes file I/O.
struct __gtos_directory {
  unsigned handle;
  struct dirent entry;
};
namespace {
int call(unsigned operation, const void *request, unsigned bytes) {
  unsigned result = operation;
  asm volatile("int $0x80" : "+a"(result)
               : "b"(reinterpret_cast<unsigned>(request)), "c"(bytes)
               : "memory", "cc");
  return static_cast<int>(result);
}
int native_error(int result) {
  switch (result) {
  case -2: return ENOENT;
  case -5: return EIO;
  case -7: return E2BIG;
  case -9: return EBADF;
  case -12: return ENOMEM;
  case -14: return EFAULT;
  case -16: return EBUSY;
  case -17: return EEXIST;
  case -19: return ENODEV;
  case -20: return ENOTDIR;
  case -21: return EISDIR;
  case -22: return EINVAL;
  case -24: return EMFILE;
  case -27: return EFBIG;
  case -28: return ENOSPC;
  case -30: return EROFS;
  case -36: return ENAMETOOLONG;
  case -38: return ENOSYS;
  case -39: return ENOTEMPTY;
  case -75: return EOVERFLOW;
  default: return EIO;
  }
}
int control(unsigned operation, unsigned handle) {
  GtosFileControlRequest request = {GTOS_FILE_ABI_VERSION, handle};
  return call(operation, &request, sizeof(request));
}
int failed(int result) {
  errno = native_error(result);
  return -1;
}
}

extern "C" DIR *opendir(const char *path) noexcept {
  if (!path) { errno = EFAULT; return nullptr; }
  unsigned bytes = 0;
  while (bytes < GTOS_FILE_PATH_LIMIT && path[bytes]) ++bytes;
  if (bytes == GTOS_FILE_PATH_LIMIT) { errno = ENAMETOOLONG; return nullptr; }
  if (!bytes) { errno = ENOENT; return nullptr; }
  auto *directory = static_cast<DIR *>(malloc(sizeof(DIR)));
  if (!directory) { errno = ENOMEM; return nullptr; }
  GtosFilePathRequest request = {GTOS_FILE_ABI_VERSION,
      reinterpret_cast<unsigned>(path), bytes + 1};
  int result = call(GTOS_SYS_FILE_DIR_OPEN, &request, sizeof(request));
  if (result <= 0) {
    free(directory);
    errno = result < 0 ? native_error(result) : EIO;
    return nullptr;
  }
  directory->handle = static_cast<unsigned>(result);
  directory->entry.d_type = DT_UNKNOWN;
  directory->entry.d_name[0] = '\0';
  return directory;
}

extern "C" struct dirent *readdir(DIR *directory) noexcept {
  if (!directory) { errno = EBADF; return nullptr; }
  GtosFileInfo info = {};
  GtosFileInfoRequest request = {GTOS_FILE_ABI_VERSION, directory->handle,
      reinterpret_cast<unsigned>(&info), sizeof(info)};
  int result = call(GTOS_SYS_FILE_DIR_READ, &request, sizeof(request));
  if (result == 0) return nullptr; // EOF preserves the caller's errno.
  if (result < 0) { failed(result); return nullptr; }
  if (result != 1 || info.version != GTOS_FILE_ABI_VERSION ||
      (info.type != GTOS_FILE_TYPE_REGULAR && info.type != GTOS_FILE_TYPE_DIRECTORY)) {
    errno = EIO;
    return nullptr;
  }
  unsigned length = 0;
  while (length < sizeof(info.name) && info.name[length]) ++length;
  if (length == sizeof(info.name)) { errno = EIO; return nullptr; }
  for (unsigned i = 0; i <= length; ++i) directory->entry.d_name[i] = info.name[i];
  directory->entry.d_type = info.type == GTOS_FILE_TYPE_DIRECTORY ? DT_DIR : DT_REG;
  return &directory->entry;
}

extern "C" int closedir(DIR *directory) noexcept {
  if (!directory) return failed(-9);
  const int result = control(GTOS_SYS_FILE_CLOSE, directory->handle);
  // Native close invalidates its handle even when the final I/O reports an
  // error. Release this stream on both paths; never retain a stale descriptor.
  free(directory);
  if (result < 0) return failed(result);
  if (result != 0) { errno = EIO; return -1; }
  return 0;
}
extern "C" void rewinddir(DIR *directory) noexcept {
  if (!directory) { failed(-9); return; }
  int result = control(GTOS_SYS_FILE_DIR_REWIND, directory->handle);
  if (result < 0) failed(result);
  else if (result != 0) errno = EIO;
}
extern "C" int dirfd(DIR *directory) noexcept {
  return directory ? static_cast<int>(directory->handle) : failed(-9);
}
extern "C" int dup(int descriptor) noexcept {
  int result = control(GTOS_SYS_FILE_DUP, static_cast<unsigned>(descriptor));
  if (result < 0) return failed(result);
  if (result == 0) { errno = EIO; return -1; }
  return result;
}
extern "C" int close(int descriptor) noexcept {
  int result = control(GTOS_SYS_FILE_CLOSE, static_cast<unsigned>(descriptor));
  if (result < 0) return failed(result);
  if (result != 0) { errno = EIO; return -1; }
  return 0;
}
