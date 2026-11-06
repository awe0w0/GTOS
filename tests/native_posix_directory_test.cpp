#include <process/file_abi.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
int error_slot, failures, checks, calls, allocations, releases;
int open_result = 41, close_result, read_error, rewind_error, malformed, cursor, peer_cursor;
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
