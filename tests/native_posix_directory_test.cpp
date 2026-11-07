#include <process/file_abi.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
int error_slot, failures, checks, calls, allocations, releases;
int open_result = 41, close_result, read_error, rewind_error, malformed, cursor, peer_cursor;
int metadata_status, metadata_shape;
unsigned info_type = GTOS_FILE_TYPE_REGULAR, info_bytes = 1234, truncate_bytes;
unsigned removed_type;
int duplicate_result = 42;
bool allocation_failure;
unsigned allocated, owned;
const char *expected_path = "/fixtures";
unsigned expected_path_bytes = 10;
alignas(16) unsigned char storage[2][1024];
void check(bool value) { ++checks; if (!value) ++failures; }
}

extern "C" int *__llvm_libc_errno() noexcept { return &error_slot; }
extern "C" void *memcpy(void *out, const void *in, size_t bytes) noexcept {
  volatile unsigned char *to = static_cast<volatile unsigned char *>(out);
  const volatile unsigned char *from = static_cast<const volatile unsigned char *>(in);
  for (size_t i = 0; i < bytes; ++i) to[i] = from[i];
  return out;
}
extern "C" void *memset(void *out, int value, size_t bytes) noexcept {
  volatile unsigned char *to = static_cast<volatile unsigned char *>(out);
  for (size_t i = 0; i < bytes; ++i) to[i] = static_cast<unsigned char>(value);
  return out;
}
extern "C" int memcmp(const void *left, const void *right, size_t bytes) noexcept {
  const unsigned char *a = static_cast<const unsigned char *>(left), *b = static_cast<const unsigned char *>(right);
  for (size_t i = 0; i < bytes; ++i) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
  return 0;
}
extern "C" void *malloc(size_t bytes) noexcept {
  ++allocations;
  if (allocation_failure) return 0;
  check(allocated != 3 && bytes <= sizeof(storage[0]));
  const unsigned slot = allocated & 1 ? 1 : 0;
  allocated |= 1U << slot;
  return storage[slot];
}
extern "C" void free(void *value) noexcept {
  const unsigned slot = value == storage[0] ? 0 : 1;
  check(value == storage[slot] && (allocated & (1U << slot)));
  allocated &= ~(1U << slot);
  ++releases;
  errno = 1234; // The wrapper must preserve the actual close/open error.
}

extern "C" int GtosPosixFileTestCall(unsigned operation, const void *data, unsigned bytes) {
  ++calls;
  if (operation == GTOS_SYS_FILE_RENAME) {
    const auto &request = *static_cast<const GtosFileRenameRequest *>(data);
    check(bytes == sizeof(request) && request.version == 1 && request.old_bytes == expected_path_bytes && request.new_bytes == 7);
    check(memcmp(reinterpret_cast<const void *>(request.old_path), expected_path, expected_path_bytes) == 0);
    check(memcmp(reinterpret_cast<const void *>(request.new_path), "/other", 7) == 0);
    return metadata_status;
  }
  if (operation == GTOS_SYS_FILE_OPEN) {
    const auto &request = *static_cast<const GtosFileOpenRequest *>(data);
    check(bytes == sizeof(request) && request.version == 1 && request.path_bytes == expected_path_bytes);
    check(memcmp(reinterpret_cast<const void *>(request.path), expected_path, expected_path_bytes) == 0);
    check(request.flags == GTOS_FILE_READ);
    return open_result;
  }
  if (operation == GTOS_SYS_FILE_DUP) {
    const auto &request = *static_cast<const GtosFileControlRequest *>(data);
    check(bytes == sizeof(request) && request.version == 1 && request.handle == 41);
    return duplicate_result;
  }
  if (operation == GTOS_SYS_FILE_MKDIR || operation == GTOS_SYS_FILE_REMOVE_TYPED) {
    const auto &request = *static_cast<const GtosFilePathRequest *>(data);
    check(request.version == 1 && request.path_bytes == expected_path_bytes);
    check(memcmp(reinterpret_cast<const void *>(request.path), expected_path, expected_path_bytes) == 0);
    if (operation == GTOS_SYS_FILE_MKDIR) check(bytes == sizeof(request));
    else {
      const auto &typed = *static_cast<const GtosFileRemoveTypedRequest *>(data);
      check(bytes == sizeof(typed) && typed.type == removed_type);
    }
    return metadata_status;
  }
  if (operation == GTOS_SYS_FILE_STAT || operation == GTOS_SYS_FILE_HANDLE_INFO) {
    unsigned output;
    if (operation == GTOS_SYS_FILE_STAT) {
      const auto &request = *static_cast<const GtosFileStatRequest *>(data);
      check(bytes == sizeof(request) && request.version == 1 && request.path_bytes == expected_path_bytes);
      check(memcmp(reinterpret_cast<const void *>(request.path), expected_path, expected_path_bytes) == 0);
      check(request.result_bytes == sizeof(GtosFileInfo));
      output = request.result;
    } else {
      const auto &request = *static_cast<const GtosFileInfoRequest *>(data);
      check(bytes == sizeof(request) && request.version == 1 && request.handle == 41);
      check(request.result_bytes == sizeof(GtosFileInfo));
      output = request.result;
    }
    if (metadata_status) return metadata_status;
    auto &info = *reinterpret_cast<GtosFileInfo *>(output);
    info.version = metadata_shape == 1 ? 0 : 1;
    info.type = metadata_shape == 2 ? 77 : info_type;
    info.bytes = info_bytes;
    return 0;
  }
  if (operation == GTOS_SYS_FILE_SYNC || operation == GTOS_SYS_FILE_TRUNCATE) {
    const auto &request = *static_cast<const GtosFileControlRequest *>(data);
    check(request.version == 1 && request.handle == 41);
    if (operation == GTOS_SYS_FILE_TRUNCATE) {
      const auto &sized = *static_cast<const GtosFileTruncateRequest *>(data);
      check(bytes == sizeof(sized) && sized.bytes == truncate_bytes);
    } else check(bytes == sizeof(request));
    return metadata_status;
  }
  if (operation == GTOS_SYS_FILE_DIR_OPEN) {
    const GtosFilePathRequest &request = *static_cast<const GtosFilePathRequest *>(data);
    check(bytes == sizeof(request) && request.version == 1 && request.path_bytes == expected_path_bytes);
    check(memcmp(reinterpret_cast<const void *>(request.path), expected_path, expected_path_bytes) == 0);
    if (open_result <= 0) return open_result;
    const unsigned slot = owned & 1 ? 1 : 0;
    check(!(owned & (1U << slot)));
    owned |= 1U << slot;
    (slot == 0 ? cursor : peer_cursor) = 0;
    return static_cast<int>(41 + slot);
  }
  if (operation == GTOS_SYS_FILE_DIR_READ) {
    const GtosFileInfoRequest &request = *static_cast<const GtosFileInfoRequest *>(data);
    check(bytes == sizeof(request) && request.version == 1 && (request.handle == 41 || request.handle == 42));
    const unsigned slot = request.handle == 41 ? 0 : 1;
    int &position = slot == 0 ? cursor : peer_cursor;
    check((owned & (1U << slot)) && request.result_bytes == sizeof(GtosFileInfo));
    if (read_error) return read_error;
    if (position == 2 && !malformed) return 0;
    GtosFileInfo &info = *reinterpret_cast<GtosFileInfo *>(request.result);
    info.version = malformed == 1 ? 0 : 1;
    info.type = malformed == 2 ? 99 : position == 0 ? GTOS_FILE_TYPE_DIRECTORY : GTOS_FILE_TYPE_REGULAR;
    if (malformed == 6) { memset(info.name, 'n', sizeof(info.name) - 1); info.name[255] = 0; }
    else if (malformed == 3) memset(info.name, 'x', sizeof(info.name));
    else if (malformed != 4) memcpy(info.name, position == 0 ? "subdir" : "a\xc3\xa9", position == 0 ? 7 : 4);
    if (!malformed) ++position;
    return malformed == 5 ? 2 : 1;
  }
  if (operation == GTOS_SYS_FILE_DIR_REWIND || operation == GTOS_SYS_FILE_CLOSE) {
    const GtosFileControlRequest &request = *static_cast<const GtosFileControlRequest *>(data);
    check(bytes == sizeof(request) && request.version == 1 && (request.handle == 41 || request.handle == 42));
    const unsigned slot = request.handle == 41 ? 0 : 1;
    check(owned & (1U << slot));
    if (operation == GTOS_SYS_FILE_DIR_REWIND) {
      if (rewind_error) return rewind_error;
      (slot == 0 ? cursor : peer_cursor) = 0;
      return 0;
    }
    owned &= ~(1U << slot); // The native close contract invalidates even on an error.
    return close_result;
  }
  check(false);
  return -22;
}

extern "C" int DirectoryTestMain() {
  const int before = calls;
  check(opendir(0) == 0 && errno == EINVAL);
  check(opendir("") == 0 && errno == ENOENT);
  char long_path[512]; memset(long_path, 'x', sizeof(long_path));
  check(opendir(long_path) == 0 && errno == 36);
  check(calls == before && allocations == 0);
  allocation_failure = true;
  check(opendir("/fixtures") == 0 && errno == ENOMEM && calls == before && !owned);
  allocation_failure = false;
  long_path[511] = 0; expected_path = long_path; expected_path_bytes = 512; open_result = -2;
  check(opendir(long_path) == 0 && errno == ENOENT && calls == before + 1 && !allocated && !owned);
  expected_path = "/fixtures"; expected_path_bytes = 10;
  open_result = -19;
  check(opendir("/fixtures") == 0 && errno == ENODEV && !allocated && !owned);
  open_result = 0;
  check(opendir("/fixtures") == 0 && errno == EIO && !allocated && !owned);
  open_result = 41;
  DIR *directory = opendir("/fixtures");
  check(directory != 0 && allocated && owned && dirfd(directory) == 41);
  struct dirent *entry = readdir(directory);
  check(entry && entry->d_type == DT_DIR && entry->d_ino == 0 && entry->d_reclen == sizeof(*entry));
  check(entry && memcmp(entry->d_name, "subdir", 7) == 0);
  entry = readdir(directory);
  check(entry && entry->d_type == DT_REG && memcmp(entry->d_name, "a\xc3\xa9", 4) == 0);
  struct dirent saved{}; if (entry) saved = *entry;
  errno = 777;
  check(readdir(directory) == 0 && errno == 777 && entry && memcmp(saved.d_name, entry->d_name, sizeof(saved.d_name)) == 0);
  for (malformed = 1; malformed <= 5; ++malformed) {
    check(readdir(directory) == 0 && errno == EIO);
    check(entry && entry->d_ino == saved.d_ino && entry->d_type == saved.d_type && entry->d_reclen == saved.d_reclen
          && memcmp(saved.d_name, entry->d_name, sizeof(saved.d_name)) == 0);
  }
  malformed = 6;
  entry = readdir(directory);
  check(entry && entry->d_type == DT_REG && entry->d_name[254] == 'n' && entry->d_name[255] == 0);
  malformed = 0;
  read_error = -5;
  check(readdir(directory) == 0 && errno == EIO);
  read_error = -12345;
  check(readdir(directory) == 0 && errno == EIO);
  read_error = 0;
  rewind_error = -19;
  rewinddir(directory); check(errno == ENODEV && cursor == 2);
  rewind_error = 0;
  rewinddir(directory); check(cursor == 0);
  entry = readdir(directory); check(entry && entry->d_type == DT_DIR);
  close_result = -5;
  check(closedir(directory) == -1 && errno == EIO && !allocated && !owned);
  close_result = 0;
  directory = opendir("/fixtures");
  errno = 456;
  check(directory && closedir(directory) == 0 && errno == 456 && !allocated && !owned);
  directory = opendir("/fixtures");
  readdir(directory);
  entry = readdir(directory);
  DIR *peer = opendir("/fixtures");
  struct dirent *peer_entry = readdir(peer);
  check(directory && peer && dirfd(directory) == 41 && dirfd(peer) == 42);
  check(entry && peer_entry && entry != peer_entry && entry->d_type == DT_REG && peer_entry->d_type == DT_DIR);
  check(entry && memcmp(entry->d_name, "a\xc3\xa9", 4) == 0);
  check(closedir(directory) == 0 && allocated == 2 && owned == 2);
  peer_entry = readdir(peer);
  check(peer_entry && peer_entry->d_type == DT_REG);
  check(closedir(peer) == 0 && !allocated && !owned);
  const int after = calls;
  check(readdir(0) == 0 && errno == EBADF);
  rewinddir(0); check(errno == EBADF);
  check(closedir(0) == -1 && errno == EBADF);
  check(dirfd(0) == -1 && errno == EBADF && calls == after);
  check(open("/fixtures", O_RDONLY | 0x40000000) == -1 && errno == EINVAL);
  check(close(0) == -1 && errno == EBADF);
  check(dup(-1) == -1 && errno == EBADF);
  check(read(-1, storage, 1) == -1 && errno == EBADF);
  check(write(-1, storage, 1) == -1 && errno == EBADF);
  check(lseek(41, 0, 999) == -1 && errno == EINVAL && calls == after);
  check(allocations == releases + 1); // The failed allocation owned no buffer.
  struct stat info, sentinel;
  memset(&sentinel, 0xA5, sizeof(sentinel));
  info = sentinel;
  const int metadata_before = calls;
  check(fstat(-1, &info) == -1 && errno == EBADF);
  check(fstat(41, 0) == -1 && errno == EINVAL);
  check(fsync(0) == -1 && errno == EBADF);
  check(ftruncate(-1, 0) == -1 && errno == EBADF);
  check(ftruncate(41, -1) == -1 && errno == EINVAL);
  if (sizeof(off_t) > 4)
    check(ftruncate(41, static_cast<off_t>(0x100000000ULL)) == -1 && errno == EFBIG);
  check(calls == metadata_before && memcmp(&info, &sentinel, sizeof(info)) == 0);
  const unsigned types[] = {GTOS_FILE_TYPE_REGULAR, GTOS_FILE_TYPE_DIRECTORY};
  for (unsigned type : types) {
    info_type = type;
    info_bytes = 0xFFFFFFFF;
    errno = 88;
    check(fstat(41, &info) == 0 && errno == 88);
    check(info.st_size == 0xFFFFFFFFLL && (type == GTOS_FILE_TYPE_REGULAR ? S_ISREG(info.st_mode) : S_ISDIR(info.st_mode)));
    check(info.st_uid == 0 && info.st_gid == 0 && info.st_ino == 0 && info.st_atime == 0);
    check(stat(expected_path, &info) == 0 && info.st_size == 0xFFFFFFFFLL && errno == 88);
  }
  const int statuses[] = {-5, -9, -19, -12345, 1};
  for (int status : statuses) {
    metadata_status = status;
    info = sentinel;
    check(fstat(41, &info) == -1 && errno == (status == -9 ? EBADF : status == -19 ? ENODEV : EIO));
    check(memcmp(&info, &sentinel, sizeof(info)) == 0);
    check(stat(expected_path, &info) == -1 && memcmp(&info, &sentinel, sizeof(info)) == 0);
    check(fsync(41) == -1 && errno == (status == -9 ? EBADF : status == -19 ? ENODEV : EIO));
    truncate_bytes = 0;
    check(ftruncate(41, 0) == -1 && errno == (status == -9 ? EBADF : status == -19 ? ENODEV : EIO));
  }
  metadata_status = 0;
  const int rename_before = calls;
  check(rename(0, "/other") == -1 && errno == EINVAL);
  check(rename(expected_path, 0) == -1 && errno == EINVAL);
  check(rename(expected_path, "") == -1 && errno == ENOENT && calls == rename_before);
  errno = 77;
  check(rename(expected_path, "/other") == 0 && errno == 77);
  const int rename_statuses[] = {-2, -5, -17, -30, 1};
  for (int status : rename_statuses) {
    metadata_status = status;
    check(rename(expected_path, "/other") == -1 && errno == (status == 1 ? EIO : -status));
  }
  metadata_status = 0;
  const int shapes[] = {1, 2};
  for (int shape : shapes) {
    metadata_shape = shape;
    info = sentinel;
    check(fstat(41, &info) == -1 && errno == EIO && memcmp(&info, &sentinel, sizeof(info)) == 0);
    check(stat(expected_path, &info) == -1 && errno == EIO && memcmp(&info, &sentinel, sizeof(info)) == 0);
  }
  metadata_shape = 0;
  errno = 66;
  check(fsync(41) == 0 && errno == 66);
  truncate_bytes = 0;
  check(ftruncate(41, 0) == 0 && errno == 66);
  truncate_bytes = 4097;
  check(ftruncate(41, 4097) == 0 && errno == 66);
  if (sizeof(off_t) > 4) {
    truncate_bytes = 0xFFFFFFFF;
    check(ftruncate(41, static_cast<off_t>(0xFFFFFFFFULL)) == 0 && errno == 66);
  }
  const int control_before = calls;
  check(fcntl(0, F_GETFD) == -1 && errno == EBADF);
  check(fcntl(41, F_DUPFD_CLOEXEC, -1) == -1 && errno == EINVAL);
  check(fcntl(41, F_DUPFD_CLOEXEC, 1) == -1 && errno == 95);
  check(fcntl(41, F_SETFD, 0) == -1 && errno == 95);
  check(fcntl(41, F_SETFD, 2) == -1 && errno == EINVAL);
  check(fcntl(41, 999) == -1 && errno == 95);
  check(access(expected_path, 7) == -1 && errno == 95);
  check(mkdir(0, 0700) == -1 && errno == EINVAL);
  check(unlink("") == -1 && errno == ENOENT);
  check(rmdir(0) == -1 && errno == EINVAL && calls == control_before);
  errno = 77;
  check(fcntl(41, F_GETFD) == FD_CLOEXEC && errno == 77);
  check(fcntl(41, F_SETFD, FD_CLOEXEC) == 0 && errno == 77);
  check(fcntl(41, F_DUPFD_CLOEXEC, 0) == 42 && errno == 77);
  duplicate_result = -24;
  check(fcntl(41, F_DUPFD_CLOEXEC, 0) == -1 && errno == EMFILE);
  duplicate_result = 42;
  check(open(expected_path, O_RDONLY | O_CLOEXEC) == 41);
  check(access(expected_path, F_OK) == 0);
  check(mkdir(expected_path, 0777) == 0);
  removed_type = GTOS_FILE_TYPE_REGULAR;
  check(unlink(expected_path) == 0);
  removed_type = GTOS_FILE_TYPE_DIRECTORY;
  check(rmdir(expected_path) == 0);
  const int control_statuses[] = {-2, -19, -20, -21, -39, 1};
  for (int status : control_statuses) {
    metadata_status = status;
    const int wanted = status == 1 ? EIO : -status;
    check(mkdir(expected_path, 0) == -1 && errno == wanted);
    removed_type = GTOS_FILE_TYPE_REGULAR;
    check(unlink(expected_path) == -1 && errno == wanted);
    removed_type = GTOS_FILE_TYPE_DIRECTORY;
    check(rmdir(expected_path) == -1 && errno == wanted);
    check(access(expected_path, F_OK) == -1 && errno == wanted);
    check(fcntl(41, F_GETFD) == -1 && errno == wanted);
  }
  metadata_status = 0;
#if defined(GTOS_DIRECTORY_TEST_BAD_EXPECTATION)
  check(cursor == 99);
#endif
  const char *text = failures ? "DIRECTORY ADAPTER FAIL\n" : "DIRECTORY ADAPTER PASS\n";
  unsigned eax = 4;
  asm volatile("int $0x80" : "+a"(eax) : "b"(1), "c"(text), "d"(23) : "memory", "cc");
  char count[16]; memcpy(count, "CHECKS ", 7);
  for (unsigned i = 0; i < 8; ++i) count[7 + i] = "0123456789ABCDEF"[(static_cast<unsigned>(checks) >> (28 - 4 * i)) & 15];
  count[15] = '\n'; eax = 4;
  asm volatile("int $0x80" : "+a"(eax) : "b"(1), "c"(count), "d"(sizeof(count)) : "memory", "cc");
  return failures ? 1 : 0;
}
