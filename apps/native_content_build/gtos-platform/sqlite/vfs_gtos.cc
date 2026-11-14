// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/sqlite/sqlite3.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>

#include <openssl/rand.h>
#include <process/file_abi.h>
#include <process/realtime_abi.h>

#include "tools/gtos_native_sdk/native-services.h"

namespace {

struct File {
  sqlite3_file base;
  unsigned handle;
  int lock;
  int last_errno;
};

constinit std::atomic<unsigned> temp_id{0};
thread_local int last_error;

template <class Request>
int Call(unsigned op, const Request& request, File* file = nullptr) {
  asm volatile("int $0x80" : "+a"(op)
               : "b"(&request), "c"(sizeof(request)) : "memory", "cc");
  const int status = static_cast<int>(op);
  if (status < 0) {
    last_error = -status;
    if (file) {
      file->last_errno = last_error;
    }
  }
  return status;
}

unsigned Address(const void* pointer) {
  return reinterpret_cast<unsigned>(pointer);
}

unsigned PathBytes(const char* name) {
  const size_t length = std::strlen(name);
  return length && length < GTOS_FILE_PATH_LIMIT
             ? static_cast<unsigned>(length + 1)
             : 0;
}

File& FromSqlite(sqlite3_file* file) {
  return *reinterpret_cast<File*>(file);
}

int Control(unsigned operation, unsigned handle, File* file = nullptr) {
  return Call(operation, GtosFileControlRequest{GTOS_FILE_ABI_VERSION, handle},
              file);
}

int OpenPath(const char* name, unsigned flags) {
  const unsigned bytes = PathBytes(name);
  if (!bytes)
    return -EINVAL;
  return Call(GTOS_SYS_FILE_OPEN,
              GtosFileOpenRequest{GTOS_FILE_ABI_VERSION, Address(name), bytes, flags});
}

int Close(sqlite3_file* file) {
  auto& state = FromSqlite(file);
  file->pMethods = nullptr;
  return Control(GTOS_SYS_FILE_CLOSE, state.handle, &state) ? SQLITE_IOERR_CLOSE
                                                          : SQLITE_OK;
}

template <class Byte>
int Transfer(sqlite3_file* file, std::span<Byte> bytes, sqlite3_int64 offset,
             unsigned operation, int failure) {
  if (offset < 0 || offset > 0x7fffffff ||
      bytes.size() > static_cast<size_t>(0x7fffffff - offset))
    return failure;
  auto& state = FromSqlite(file);
  while (!bytes.empty()) {
    const size_t chunk = std::min<size_t>(bytes.size(), GTOS_FILE_TRANSFER_LIMIT);
    const int count = Call(operation, GtosFilePositionedTransferRequest{
        GTOS_FILE_ABI_VERSION, state.handle, Address(bytes.data()),
        static_cast<unsigned>(chunk), static_cast<unsigned>(offset), 0}, &state);
    if (count < 0 || static_cast<size_t>(count) > chunk)
      return failure;
    bytes = bytes.subspan(static_cast<size_t>(count));
    if (static_cast<size_t>(count) != chunk) {
      if constexpr (std::is_const_v<Byte>) {
        return SQLITE_IOERR_WRITE;
      } else {
        std::ranges::fill(bytes, std::byte{0});
        return SQLITE_IOERR_SHORT_READ;
      }
    }
    offset += count;
  }
  return SQLITE_OK;
}

int Read(sqlite3_file* file, void* data, int count, sqlite3_int64 offset) {
  if (count < 0 || (!data && count))
    return SQLITE_IOERR_READ;
  return Transfer(file, std::span(static_cast<std::byte*>(data), static_cast<size_t>(count)),
                  offset, GTOS_SYS_FILE_READ_AT, SQLITE_IOERR_READ);
}

int Write(sqlite3_file* file, const void* data, int count, sqlite3_int64 offset) {
  if (count < 0 || (!data && count))
    return SQLITE_IOERR_WRITE;
  return Transfer(file, std::span(static_cast<const std::byte*>(data),
                                 static_cast<size_t>(count)),
                  offset, GTOS_SYS_FILE_WRITE_AT, SQLITE_IOERR_WRITE);
}

int Truncate(sqlite3_file* file, sqlite3_int64 size) {
  if (size < 0 || size > 0x7fffffff)
    return SQLITE_IOERR_TRUNCATE;
  auto& state = FromSqlite(file);
  return Call(GTOS_SYS_FILE_TRUNCATE,
              GtosFileTruncateRequest{GTOS_FILE_ABI_VERSION, state.handle,
                                      static_cast<unsigned>(size)},
              &state)
             ? SQLITE_IOERR_TRUNCATE
             : SQLITE_OK;
}

int Sync(sqlite3_file* file, int) {
  auto& state = FromSqlite(file);
  return Control(GTOS_SYS_FILE_SYNC, state.handle, &state) ? SQLITE_IOERR_FSYNC
                                                         : SQLITE_OK;
}

int FileSize(sqlite3_file* file, sqlite3_int64* size) {
  auto& state = FromSqlite(file);
  const int result = Control(GTOS_SYS_FILE_SIZE, state.handle, &state);
  if (result < 0)
    return SQLITE_IOERR_FSTAT;
  *size = result;
  return SQLITE_OK;
}

// Main databases hold a native file lock from xOpen to xClose. SQLite still
// tracks its own transaction lock transitions and rollback journal state.
int Lock(sqlite3_file* file, int level) {
  auto& state = FromSqlite(file);
  state.lock = std::max(state.lock, level);
  return SQLITE_OK;
}

int Unlock(sqlite3_file* file, int level) {
  FromSqlite(file).lock = level;
  return SQLITE_OK;
}

int CheckReservedLock(sqlite3_file* file, int* result) {
  *result = FromSqlite(file).lock >= SQLITE_LOCK_RESERVED;
  return SQLITE_OK;
}

int FileControl(sqlite3_file* file, int op, void* result) {
  const auto& state = FromSqlite(file);
  switch (op) {
    case SQLITE_FCNTL_LOCKSTATE:
      *static_cast<int*>(result) = state.lock;
      return SQLITE_OK;
    case SQLITE_FCNTL_LAST_ERRNO:
      *static_cast<int*>(result) = state.last_errno;
      return SQLITE_OK;
    default:
      return SQLITE_NOTFOUND;
  }
}

const sqlite3_io_methods kIo{
    .iVersion = 1, .xClose = Close, .xRead = Read, .xWrite = Write,
    .xTruncate = Truncate, .xSync = Sync, .xFileSize = FileSize,
    .xLock = Lock, .xUnlock = Unlock, .xCheckReservedLock = CheckReservedLock,
    .xFileControl = FileControl,
    .xSectorSize = [](sqlite3_file*) { return 512; },
    .xDeviceCharacteristics = [](sqlite3_file*) { return 0; },
    .xShmMap = nullptr, .xShmLock = nullptr, .xShmBarrier = nullptr,
    .xShmUnmap = nullptr, .xFetch = nullptr, .xUnfetch = nullptr,
};

int Open(sqlite3_vfs*, sqlite3_filename name, sqlite3_file* file,
         int flags, int* output_flags) {
  auto& state = *new (file) File{};
  const bool temporary = !name || (flags & SQLITE_OPEN_DELETEONCLOSE);
  unsigned mode = (flags & SQLITE_OPEN_READWRITE) ? GTOS_FILE_READ_WRITE : GTOS_FILE_READ;
  if (flags & SQLITE_OPEN_CREATE)
    mode |= GTOS_FILE_CREATE;
  if (flags & SQLITE_OPEN_EXCLUSIVE)
    mode |= GTOS_FILE_EXCLUSIVE;
  if (!temporary && (flags & SQLITE_OPEN_MAIN_DB))
    mode |= GTOS_FILE_LOCKED;
  if (temporary && !(flags & SQLITE_OPEN_READWRITE))
    return SQLITE_CANTOPEN;

  int handle;
  if (temporary) {
    constexpr unsigned temporary_mode =
        GTOS_FILE_READ_WRITE | GTOS_FILE_CREATE | GTOS_FILE_EXCLUSIVE | GTOS_FILE_TEMPORARY;
    handle = -EEXIST;
    const unsigned process = gtos::native::CurrentProcessInfo().process_id;
    for (unsigned attempt = 0; attempt < 16 && handle == -EEXIST; ++attempt) {
      std::array<char, 40> path{};
      std::snprintf(path.data(), path.size(), "/.sqlite-%08x-%08x", process,
                    temp_id.fetch_add(1, std::memory_order_relaxed));
      handle = OpenPath(path.data(), temporary_mode);
    }
  } else {
    handle = OpenPath(name, mode);
    if (handle == -EROFS && (flags & SQLITE_OPEN_READWRITE) &&
        !(flags & SQLITE_OPEN_EXCLUSIVE)) {
      flags = (flags & ~(SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)) | SQLITE_OPEN_READONLY;
      handle = OpenPath(name, GTOS_FILE_READ | (mode & GTOS_FILE_LOCKED));
    }
  }
  if (handle <= 0)
    return handle == -EBUSY ? SQLITE_BUSY : SQLITE_CANTOPEN;
  state.handle = static_cast<unsigned>(handle);
  state.base.pMethods = &kIo;
  if (output_flags)
    *output_flags = flags;
  return SQLITE_OK;
}

int Delete(sqlite3_vfs*, const char* name, int) {
  const unsigned bytes = PathBytes(name);
  if (!bytes)
    return SQLITE_IOERR_DELETE;
  const int result = Call(GTOS_SYS_FILE_REMOVE_TYPED, GtosFileRemoveTypedRequest{
      GTOS_FILE_ABI_VERSION, Address(name), bytes, GTOS_FILE_TYPE_REGULAR});
  if (result == -ENOENT)
    return SQLITE_IOERR_DELETE_NOENT;
  return result ? SQLITE_IOERR_DELETE : SQLITE_OK;
}

int Access(sqlite3_vfs*, const char* name, int flags, int* accessible) {
  *accessible = 0;
  const unsigned bytes = PathBytes(name);
  if (!bytes)
    return SQLITE_IOERR_ACCESS;
  GtosFileInfo info{};
  const int result = Call(GTOS_SYS_FILE_STAT, GtosFileStatRequest{
      GTOS_FILE_ABI_VERSION, Address(name), bytes, Address(&info), sizeof(info)});
  if (result == -ENOENT)
    return SQLITE_OK;
  if (result)
    return SQLITE_IOERR_ACCESS;
  if (flags == SQLITE_ACCESS_READWRITE) {
    if (info.type != GTOS_FILE_TYPE_REGULAR)
      return SQLITE_OK;
    const int handle = OpenPath(name, GTOS_FILE_READ_WRITE);
    if (handle == -EROFS)
      return SQLITE_OK;
    if (handle <= 0 || Control(GTOS_SYS_FILE_CLOSE, static_cast<unsigned>(handle)))
      return SQLITE_IOERR_ACCESS;
  }
  *accessible = 1;
  return SQLITE_OK;
}

int FullPathname(sqlite3_vfs*, const char* name, int capacity, char* output) {
  if (!PathBytes(name) || capacity < 2)
    return SQLITE_CANTOPEN;
  std::string_view remaining(name);
  std::array<char, GTOS_FILE_PATH_LIMIT> path{};
  path[0] = '/';
  size_t used = 1;
  while (!remaining.empty()) {
    const size_t separator = remaining.find('/');
    const auto component = remaining.substr(0, separator);
    remaining = separator == std::string_view::npos
                    ? std::string_view() : remaining.substr(separator + 1);
    if (component.empty() || component == ".")
      continue;
    if (component == "..") {
      while (used > 1 && path[used - 1] != '/')
        --used;
      if (used > 1)
        --used;
      continue;
    }
    const size_t delimiter = used > 1 ? 1 : 0;
    if (component.size() + delimiter >= path.size() - used)
      return SQLITE_CANTOPEN;
    if (delimiter)
      path[used++] = '/';
    std::ranges::copy(component, std::span(path).subspan(used).begin());
    used += component.size();
  }
  if (used >= static_cast<size_t>(capacity))
    return SQLITE_CANTOPEN;
  path[used] = '\0';
  std::memcpy(output, path.data(), used + 1);
  return SQLITE_OK;
}

int CurrentTimeInt64(sqlite3_vfs*, sqlite3_int64* time) {
  GtosRealtimeReadResult result{};
  if (Call(GTOS_SYS_REALTIME_READ, GtosRealtimeReadRequest{
          GTOS_REALTIME_ABI_VERSION, 0, Address(&result), sizeof(result)}) ||
      result.version != GTOS_REALTIME_ABI_VERSION ||
      result.unit != GTOS_REALTIME_UNIT_MICROSECONDS ||
      result.source != GTOS_REALTIME_SOURCE_CMOS_PIT ||
      (result.capabilities & GTOS_REALTIME_REQUIRED_CAPABILITIES) !=
          GTOS_REALTIME_REQUIRED_CAPABILITIES)
    return SQLITE_ERROR;
  constexpr sqlite3_int64 kUnixEpochJulianMilliseconds = 210866760000000LL;
  *time = kUnixEpochJulianMilliseconds + static_cast<sqlite3_int64>(result.microseconds / 1000);
  return SQLITE_OK;
}

int CurrentTime(sqlite3_vfs* vfs, double* time) {
  sqlite3_int64 milliseconds;
  const int status = CurrentTimeInt64(vfs, &milliseconds);
  if (status == SQLITE_OK)
    *time = static_cast<double>(milliseconds) / 86400000.0;
  return status;
}

int Randomness(sqlite3_vfs*, int bytes, char* output) {
  return bytes > 0 && RAND_bytes(reinterpret_cast<uint8_t*>(output), static_cast<size_t>(bytes))
             ? bytes : 0;
}

int Sleep(sqlite3_vfs*, int microseconds) {
  if (microseconds <= 0)
    return 0;
  std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
  return microseconds;
}

int LastError(sqlite3_vfs*, int bytes, char* output) {
  if (bytes > 0)
    *output = '\0';
  return last_error;
}

sqlite3_vfs vfs{
    .iVersion = 3, .szOsFile = sizeof(File), .mxPathname = GTOS_FILE_PATH_LIMIT - 1,
    .pNext = nullptr, .zName = "gtos", .pAppData = nullptr,
    .xOpen = Open, .xDelete = Delete, .xAccess = Access, .xFullPathname = FullPathname,
    .xDlOpen = nullptr, .xDlError = nullptr, .xDlSym = nullptr, .xDlClose = nullptr,
    .xRandomness = Randomness, .xSleep = Sleep, .xCurrentTime = CurrentTime,
    .xGetLastError = LastError, .xCurrentTimeInt64 = CurrentTimeInt64,
    .xSetSystemCall = nullptr, .xGetSystemCall = nullptr, .xNextSystemCall = nullptr,
};

}  // namespace

extern "C" int sqlite3_os_init() {
  return sqlite3_vfs_register(&vfs, 1);
}

extern "C" int sqlite3_os_end() {
  return sqlite3_vfs_unregister(&vfs);
}
