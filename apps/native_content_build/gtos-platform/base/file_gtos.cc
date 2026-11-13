// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <bit>
#include <limits>

#include <process/file_abi.h>

#include "base/files/file_path.h"
#include "base/threading/scoped_blocking_call.h"

namespace base {
namespace {

template <class Byte, class Transfer>
int TransferAll(span<Byte> bytes, Transfer transfer) {
  size_t done = 0;
  do {
    const ssize_t result = transfer(bytes.subspan(done), done);
    if (result <= 0) {
      return done ? static_cast<int>(done) : static_cast<int>(result);
    }
    done += static_cast<size_t>(result);
  } while (done < bytes.size());
  return static_cast<int>(done);
}

int64_t Position(int64_t offset, size_t done) {
  const int64_t count = done;
  const int64_t max = std::numeric_limits<int64_t>::max();
  return offset > max - count ? max : offset + count;
}

std::optional<size_t> TransferCount(ssize_t result) {
  if (result < 0) {
    return std::nullopt;
  }
  return static_cast<size_t>(result);
}

}  // namespace

bool File::IsValid() const {
  return file_.is_valid();
}

PlatformFile File::GetPlatformFile() const {
  return file_.get();
}

PlatformFile File::TakePlatformFile() {
  return file_.release();
}

void File::SetPlatformFile(PlatformFile file) {
  file_.reset(file);
}

void File::Close() {
  if (IsValid()) {
    ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
    file_.reset();
  }
}

void File::DoInitialize(const FilePath& path, uint32_t flags) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  constexpr uint32_t kDisposition = FLAG_OPEN | FLAG_CREATE | FLAG_OPEN_ALWAYS |
                                    FLAG_CREATE_ALWAYS | FLAG_OPEN_TRUNCATED;
  // Chromium supplies Windows-only sharing hints at cross-platform call sites.
  // The native store uses its ordinary open rules for those hints.
  constexpr uint32_t kWindowsHints = FLAG_WIN_SHARE_DELETE |
                                    FLAG_WIN_EXCLUSIVE_WRITE |
                                    FLAG_WIN_NO_EXECUTE;
  // Native paths have no symlinks, and a file handle grants no execute access.
  constexpr uint32_t kSupported = kDisposition | FLAG_READ | FLAG_WRITE |
                                  FLAG_APPEND | FLAG_NO_FOLLOW | kWindowsHints;
  const uint32_t disposition = flags & kDisposition;
  const bool can_read = (flags & FLAG_READ) != 0;
  const bool can_write = (flags & (FLAG_WRITE | FLAG_APPEND)) != 0;
  if ((flags & ~kSupported) || !std::has_single_bit(disposition) ||
      (!can_read && !can_write) ||
      ((flags & FLAG_WRITE) && (flags & FLAG_APPEND)) ||
      (!can_write && (disposition == FLAG_CREATE_ALWAYS ||
                      disposition == FLAG_OPEN_TRUNCATED))) {
    errno = EINVAL;
    error_details_ = FILE_ERROR_INVALID_OPERATION;
    return;
  }
  const int access = can_write ? (can_read ? O_RDWR : O_WRONLY) : O_RDONLY;
  const int mode = access | (flags & FLAG_APPEND ? O_APPEND : 0);
  const char* name = path.value().c_str();
  int fd;
  if (disposition == FLAG_OPEN_ALWAYS) {
    // Existing files can be opened even when the volume disallows creation.
    // Exclusive creation distinguishes this open from a concurrent creator.
    fd = open(name, mode);
    if (fd < 0 && errno == ENOENT) {
      fd = open(name, mode | O_CREAT | O_EXCL);
      created_ = fd >= 0;
      if (fd < 0 && errno == EEXIST) {
        fd = open(name, mode);
      }
    }
  } else {
    int open_flags = mode;
    if (disposition == FLAG_CREATE) {
      open_flags |= O_CREAT | O_EXCL;
    } else if (disposition == FLAG_CREATE_ALWAYS) {
      open_flags |= O_CREAT | O_TRUNC;
    } else if (disposition == FLAG_OPEN_TRUNCATED) {
      open_flags |= O_TRUNC;
    }
    fd = open(name, open_flags);
    created_ = fd >= 0 && (disposition == FLAG_CREATE ||
                           disposition == FLAG_CREATE_ALWAYS);
  }
  if (fd < 0) {
    error_details_ = GetLastFileError();
    return;
  }
  SetPlatformFile(fd);
  error_details_ = FILE_OK;
}

int64_t File::Seek(Whence whence, int64_t offset) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  return lseek(file_.get(), offset, whence);
}

int File::Read(int64_t offset, char* data, int size) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  if (size < 0) {
    errno = EINVAL;
    return -1;
  }
  return TransferAll(UNSAFE_BUFFERS(span(data, static_cast<size_t>(size))),
                     [&](span<char> next, size_t done) {
                       return gtos_read_at(file_.get(), next.data(), next.size(),
                                           Position(offset, done));
                     });
}

int File::ReadAtCurrentPos(char* data, int size) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  if (size < 0) {
    errno = EINVAL;
    return -1;
  }
  char empty;
  return TransferAll(UNSAFE_BUFFERS(span(data, static_cast<size_t>(size))),
                     [&](span<char> next, size_t) {
                       return read(file_.get(), next.empty() ? &empty : next.data(),
                                   next.size());
                     });
}

std::optional<size_t> File::ReadNoBestEffort(int64_t offset, span<uint8_t> data) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  return TransferCount(gtos_read_at(file_.get(), data.data(), data.size(), offset));
}

std::optional<size_t> File::ReadAtCurrentPosNoBestEffort(span<uint8_t> data) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  uint8_t empty;
  return TransferCount(read(file_.get(), data.empty() ? &empty : data.data(), data.size()));
}

int File::Write(int64_t offset, const char* data, int size) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  if (size < 0) {
    errno = EINVAL;
    return -1;
  }
  return TransferAll(UNSAFE_BUFFERS(span(data, static_cast<size_t>(size))),
                     [&](span<const char> next, size_t done) {
                       return gtos_write_at(file_.get(), next.data(), next.size(),
                                            Position(offset, done));
                     });
}

int File::WriteAtCurrentPos(const char* data, int size) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  if (size < 0) {
    errno = EINVAL;
    return -1;
  }
  char empty;
  return static_cast<int>(write(file_.get(), size ? data : &empty,
                                static_cast<size_t>(size)));
}

std::optional<size_t> File::WriteAtCurrentPosNoBestEffort(span<const uint8_t> data) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  uint8_t empty;
  const auto size = std::min<size_t>(data.size(), GTOS_FILE_TRANSFER_LIMIT);
  return TransferCount(write(file_.get(), data.empty() ? &empty : data.data(), size));
}

int64_t File::GetLength() const {
  Info info;
  return GetInfo(&info) && !info.is_directory ? info.size : -1;
}

bool File::SetLength(int64_t length) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  return ftruncate(file_.get(), length) == 0;
}

bool File::Flush() {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  return fsync(file_.get()) == 0;
}

bool File::SetTimes(Time, Time) {
  errno = ENOSYS;
  return false;
}

bool File::GetInfo(Info* info) const {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  struct stat value {};
  if (fstat(file_.get(), &value) != 0) {
    return false;
  }
  Info result;
  result.size = value.st_size;
  result.is_directory = S_ISDIR(value.st_mode);
  *info = result;
  return true;
}

File::Error File::Lock(LockMode) {
  errno = ENOSYS;
  return FILE_ERROR_INVALID_OPERATION;
}

File::Error File::Unlock() {
  errno = ENOSYS;
  return FILE_ERROR_INVALID_OPERATION;
}

File File::Duplicate() const {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  const int handle = dup(file_.get());
  return handle < 0 ? File(GetLastFileError()) : File(handle);
}

File::Error File::OSErrorToFileError(int error) {
  switch (error) {
    case ENOENT: return FILE_ERROR_NOT_FOUND;
    case EEXIST: return FILE_ERROR_EXISTS;
    case ENOTDIR: return FILE_ERROR_NOT_A_DIRECTORY;
    case EISDIR: return FILE_ERROR_NOT_A_FILE;
    case EACCES: return FILE_ERROR_ACCESS_DENIED;
    case EBUSY: return FILE_ERROR_IN_USE;
    case EMFILE: return FILE_ERROR_TOO_MANY_OPENED;
    case ENOMEM: return FILE_ERROR_NO_MEMORY;
    case ENOSPC: return FILE_ERROR_NO_SPACE;
    case ENOTEMPTY: return FILE_ERROR_NOT_EMPTY;
    case EINVAL:
    case EBADF:
    case ENOSYS: return FILE_ERROR_INVALID_OPERATION;
    case EIO: return FILE_ERROR_IO;
    default: return FILE_ERROR_FAILED;
  }
}

File::Error File::GetLastFileError() {
  return OSErrorToFileError(errno);
}

}  // namespace base
