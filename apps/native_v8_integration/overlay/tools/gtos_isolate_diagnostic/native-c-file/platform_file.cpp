#include "src/__support/File/file.h"
#include "src/__support/CPP/new.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/libc_errno.h"
#include "hdr/stdio_macros.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"
#include "src/stdio/stdin.h"
#include "src/stdio/stdout.h"
#include "src/stdio/stderr.h"
#include <process/abi.h>
#include <process/file_abi.h>

#if !defined(__GTOS__) || !defined(__i386__) || defined(__linux__)
#error This port requires the native GTOS IA32 ABI
#endif
static_assert(sizeof(void*) == 4 && sizeof(size_t) == 4 && sizeof(off_t) == 8,
              "Native FILE ABI");
namespace LIBC_NAMESPACE_DECL {
namespace {
int call(unsigned op, const void *request, unsigned bytes) {
  unsigned result = op;
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
ErrorOr<unsigned> open_flags(const FileMode &mode) {
  if (!mode.is_valid()) return Error(EINVAL);
  unsigned flags = mode.is_update() ? GTOS_FILE_READ_WRITE :
                   mode.write_allowed() ? GTOS_FILE_WRITE : GTOS_FILE_READ;
  if (mode.is_write()) flags |= GTOS_FILE_CREATE | GTOS_FILE_TRUNCATE;
  if (mode.is_append()) flags |= GTOS_FILE_CREATE | GTOS_FILE_APPEND;
  if (mode.is_exclusive_create()) {
    if (!(flags & GTOS_FILE_CREATE)) return Error(EINVAL);
    flags |= GTOS_FILE_EXCLUSIVE;
  }
  return flags;
}
ErrorOr<unsigned> path_bytes(const char *path) {
  for (unsigned i = 0; i < GTOS_FILE_PATH_LIMIT; ++i) {
    if (path[i] == '\0') {
      if (i == 0) return Error(EINVAL);
      return i + 1;
    }
  }
  return Error(ENAMETOOLONG);
}
int close_handle(unsigned handle) {
  GtosFileControlRequest request = {GTOS_FILE_ABI_VERSION, handle};
  int result = call(GTOS_SYS_FILE_CLOSE, &request, sizeof(request));
  return result < 0 ? native_error(result) : 0;
}
}
class GtosFile : public File {
  unsigned handle;
  unsigned console;
  const bool permanent;
public:
  static FileIOResult transfer(File *file, void *buffer, size_t bytes, unsigned op) {
    auto *f = static_cast<GtosFile *>(file);
    unsigned address = reinterpret_cast<unsigned>(buffer);
    if (bytes > ~0U - address) return {0, EOVERFLOW};
    if (f->console) {
      // Native console input has no read service yet: report an error, not EOF.
      if (op == GTOS_SYS_FILE_READ) return {0, ENOSYS};
      if (f->console == 1) return {0, EBADF};
      size_t completed = 0;
      while (completed < bytes) {
        unsigned count = bytes - completed;
        if (count > GTOS_NATIVE_WRITE_LIMIT) count = GTOS_NATIVE_WRITE_LIMIT;
        int result = call(GTOS_SYS_WRITE,
                          reinterpret_cast<void *>(address + completed), count);
        if (result < 0) return {completed, result == GTOS_ERR_BAD_ADDRESS ? EFAULT :
                                         result == GTOS_ERR_TOO_LARGE ? E2BIG : EIO};
        if (result == 0) return {completed, EIO};
        if (static_cast<unsigned>(result) > count) __builtin_trap();
        completed += result;
      }
      return completed;
    }
    if (!f->handle) return {0, EBADF};
    size_t completed = 0;
    while (completed < bytes) {
      unsigned count = bytes - completed;
      if (count > GTOS_FILE_TRANSFER_LIMIT) count = GTOS_FILE_TRANSFER_LIMIT;
      GtosFileTransferRequest request = {
          GTOS_FILE_ABI_VERSION, f->handle, address + completed, count};
      int result = call(op, &request, sizeof(request));
      if (result < 0) return {completed, native_error(result)};
      if (static_cast<unsigned>(result) > count) __builtin_trap();
      completed += result;
      if (static_cast<unsigned>(result) != count) {
        if (op == GTOS_SYS_FILE_WRITE) return {completed, EIO};
        break;
      }
    }
    return completed;
  }
  static FileIOResult write(File *f, const void *buffer, size_t bytes) {
    return transfer(f, const_cast<void *>(buffer), bytes, GTOS_SYS_FILE_WRITE);
  }
  static FileIOResult read(File *f, void *buffer, size_t bytes) {
    return transfer(f, buffer, bytes, GTOS_SYS_FILE_READ);
  }
  static ErrorOr<off_t> seek(File *file, off_t offset, int whence) {
    auto *f = static_cast<GtosFile *>(file);
    if (f->console) return Error(ESPIPE);
    if (!f->handle) return Error(EBADF);
    uint64_t value = static_cast<uint64_t>(offset);
    GtosFileSeekRequest request = {GTOS_FILE_ABI_VERSION, f->handle,
        static_cast<unsigned>(value), static_cast<unsigned>(value >> 32),
        static_cast<unsigned>(whence)};
    int result = call(GTOS_SYS_FILE_SEEK, &request, sizeof(request));
    if (result < 0) return Error(native_error(result));
    return static_cast<off_t>(result);
  }
  static int close(File *file) {
    auto *f = static_cast<GtosFile *>(file);
    File::remove_file(f);
    int result = f->handle ? close_handle(f->handle) : f->console ? 0 : EBADF;
    f->handle = 0;
    f->console = 0;
    if (f->permanent) {
      // File::close already releases an owned setvbuf buffer. The static FILE
      // stays in its process image, and fflush(NULL) must stop visiting it.
      auto *stream = reinterpret_cast<::FILE *>(f);
      if (stdin == stream) stdin = nullptr;
      if (stdout == stream) stdout = nullptr;
      if (stderr == stream) stderr = nullptr;
    } else {
      delete f;
    }
    return result;
  }
  static int flush(File *file) {
    auto *f = static_cast<GtosFile *>(file);
    // Console writes are submitted synchronously by the native write service.
    if (f->console) return 0;
    if (!f->handle) return EBADF;
    GtosFileControlRequest request = {GTOS_FILE_ABI_VERSION, f->handle};
    int result = call(GTOS_SYS_FILE_SYNC, &request, sizeof(request));
    return result < 0 ? native_error(result) : 0;
  }
  GtosFile(uint8_t *buffer, FileMode mode)
      : File(write, read, seek, close, buffer, DEFAULT_BUFFER_SIZE, _IOFBF,
             true, mode), handle(0), console(0), permanent(false) {}
  constexpr GtosFile(unsigned kind)
      : File(write, read, seek, close, nullptr, 0, _IONBF, false,
             FileMode(kind == 1 ? "r" : "a")),
        handle(0), console(kind), permanent(true) {}
  int descriptor() const {
    return handle ? static_cast<int>(handle) : console ? -ENOSYS : -EBADF;
  }
  void set_handle(unsigned value) { handle = value; }
  int reopen(const char *path, const char *mode_string) {
    // Close the old native handle even if its buffered write fails.
    int flush_error = flush_unlocked();
    if (handle) close_handle(handle);
    handle = 0;
    console = 0;
    FileMode mode(mode_string);
    reset_stream_state_unlocked(mode);
    if (flush_error) return flush_error;
    auto flags = open_flags(mode);
    if (!flags.has_value()) return flags.error();
    // The native ABI has no operation to change a live handle's access flags.
    if (!path) return ENOSYS;
    auto length = path_bytes(path);
    if (!length.has_value()) return length.error();
    GtosFileOpenRequest request = {GTOS_FILE_ABI_VERSION,
        reinterpret_cast<unsigned>(path), length.value(), flags.value()};
    int result = call(GTOS_SYS_FILE_OPEN, &request, sizeof(request));
    if (result < 0) return native_error(result);
    handle = result;
    return 0;
  }
};
// Constant-initialized, unbuffered diagnostic console streams. Both the public
// C variables and LLVM's internal names refer to these same FILE pointers.
static GtosFile StdIn(1);
static GtosFile StdOut(2);
static GtosFile StdErr(3);
LLVM_LIBC_VARIABLE(::FILE *, stdin) = reinterpret_cast<::FILE *>(&StdIn);
LLVM_LIBC_VARIABLE(::FILE *, stdout) = reinterpret_cast<::FILE *>(&StdOut);
LLVM_LIBC_VARIABLE(::FILE *, stderr) = reinterpret_cast<::FILE *>(&StdErr);
ErrorOr<File *> openfile(const char *path, const char *mode_string) {
  FileMode mode(mode_string);
  auto flags = open_flags(mode);
  if (!flags.has_value()) return Error(flags.error());
  auto length = path_bytes(path);
  if (!length.has_value()) return Error(length.error());
  // Allocate before opening so OOM cannot create or truncate a disk file.
  AllocChecker buffer_check;
  uint8_t *buffer = new (buffer_check) uint8_t[File::DEFAULT_BUFFER_SIZE];
  if (!buffer_check) return Error(ENOMEM);
  AllocChecker file_check;
  auto *file = new (file_check) GtosFile(buffer, mode);
  if (!file_check) {
    delete[] buffer;
    return Error(ENOMEM);
  }
  GtosFileOpenRequest request = {GTOS_FILE_ABI_VERSION,
      reinterpret_cast<unsigned>(path), length.value(), flags.value()};
  int result = call(GTOS_SYS_FILE_OPEN, &request, sizeof(request));
  if (result < 0) {
    delete[] buffer;
    delete file;
    return Error(native_error(result));
  }
  file->set_handle(result);
  File::add_file(file);
  return static_cast<File *>(file);
}
int get_fileno(File *file) {
  int result = static_cast<GtosFile *>(file)->descriptor();
  if (result < 0) {
    libc_errno = -result;
    return -1;
  }
  return result;
}
int reopenfile_unlocked(File *file, const char *path, const char *mode) {
  return static_cast<GtosFile *>(file)->reopen(path, mode);
}
int reopenfile(File *file, const char *path, const char *mode) {
  File::FileLock lock(file);
  return reopenfile_unlocked(file, path, mode);
}
int flushfile_unlocked(File *file) { return GtosFile::flush(file); }
} // namespace LIBC_NAMESPACE_DECL
