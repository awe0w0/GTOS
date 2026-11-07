#include "path_record.h"
#include <process/file_abi.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" volatile NativePathRecord native_path_record
    __attribute__((section(".data.vm_record"), used, aligned(4))) = {1, GTOS_PATH_GUEST_MODE, 0, 0, 0, 0};

namespace {
void Write(const char* text) {
  unsigned bytes = 0;
  while (text[bytes]) ++bytes;
  unsigned operation = 0x4701;
  asm volatile("int $0x80" : "+a"(operation) : "b"(text), "c"(bytes) : "memory", "cc");
}
void Check(bool value) {
  native_path_record.checks = native_path_record.checks + 1;
  if (!value) native_path_record.failures = native_path_record.failures + 1;
}
int Raw(unsigned operation, const void* request, unsigned bytes) {
  native_path_record.raw_calls = native_path_record.raw_calls + 1;
  asm volatile("int $0x80" : "+a"(operation) : "b"(request), "c"(bytes) : "memory", "cc");
  return static_cast<int>(operation);
}
void Reject(const GtosFileRemoveTypedRequest* request, unsigned bytes, int error) {
  Check(Raw(GTOS_SYS_FILE_REMOVE_TYPED, request, bytes) == error);
  struct stat value;
  Check(stat("/typed", &value) == 0 && S_ISREG(value.st_mode));
}
}

extern "C" int NativeEntry() {
  Write("PATH CONSUMER BEGIN\n");
#if GTOS_PATH_GUEST_MODE == 0
  struct stat value;
  Check(mkdir("/paths", 0700) == 0);
  Check(access("/paths", F_OK) == 0);
  Check(stat("/paths", &value) == 0 && S_ISDIR(value.st_mode));
  Check(unlink("/paths") == -1 && errno == EISDIR);
  Check(stat("/paths", &value) == 0 && S_ISDIR(value.st_mode));
  int fd = open("/paths/first", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  Check(fd > 0);
  Check(fcntl(fd, F_GETFD) == FD_CLOEXEC);
  Check(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
  Check(fcntl(fd, F_SETFD, 0) == -1 && errno == 95);
  Check(write(fd, "abcdef", 6) == 6);
  Check(fsync(fd) == 0);
  Check(fstat(fd, &value) == 0 && S_ISREG(value.st_mode) && value.st_size == 6);
  const int peer = fcntl(fd, F_DUPFD_CLOEXEC, 0);
  Check(peer > 0 && peer != fd);
  Check(lseek(fd, 0, SEEK_SET) == 0);
  char bytes[4] = {};
  Check(read(peer, bytes, 2) == 2 && bytes[0] == 'a' && bytes[1] == 'b');
  Check(lseek(fd, 0, SEEK_CUR) == 2);
  Check(close(peer) == 0);
  Check(ftruncate(fd, 3) == 0);
  Check(fsync(fd) == 0);
  Check(close(fd) == 0);
  Check(rmdir("/paths/first") == -1 && errno == ENOTDIR);
  Check(stat("/paths/first", &value) == 0 && S_ISREG(value.st_mode) && value.st_size == 3);
  Check(rmdir("/paths") == -1 && errno == 39);
  Check(rename("/paths/first", "/paths/renamed") == 0);
  Check(access("/paths/first", F_OK) == -1 && errno == ENOENT);
  Check(stat("/paths/renamed", &value) == 0 && value.st_size == 3);
  Check(unlink("/paths/renamed") == 0);
  Check(access("/paths/first", F_OK) == -1 && errno == ENOENT);
  Check(rmdir("/paths") == 0);
  Check(access("/paths", F_OK) == -1 && errno == ENOENT);
  fd = open("/typed", O_WRONLY | O_CREAT | O_EXCL, 0600);
  Check(fd > 0);
  Check(close(fd) == 0);
  GtosFileRemoveTypedRequest request{1, reinterpret_cast<unsigned>("/typed"), 7, GTOS_FILE_TYPE_REGULAR};
  Reject(&request, 15, -22);
  Reject(&request, 17, -22);
  request.version = 2; Reject(&request, 16, -38); request.version = 1;
  request.type = 0; request.path = 0; Reject(&request, 16, -22);
  request.type = 99; Reject(&request, 16, -22);
  request.type = GTOS_FILE_TYPE_REGULAR; Reject(&request, 16, -14);
  request.path = reinterpret_cast<unsigned>("/typed");
  request.path_bytes = 0; Reject(&request, 16, -22);
  request.path_bytes = 8; Reject(&request, 16, -22);
  Reject(nullptr, 16, -14);
  Reject(nullptr, 15, -22);
  Check(mkdir("/legacy", 0777) == 0);
  const GtosFilePathRequest old_request{1, reinterpret_cast<unsigned>("/legacy"), 8};
  Check(Raw(GTOS_SYS_FILE_REMOVE, &old_request, sizeof(old_request)) == 0);
  Check(access("/legacy", F_OK) == -1 && errno == ENOENT);
  Check(unlink("/typed") == 0);
  Check(access("/typed", F_OK) == -1 && errno == ENOENT);
#else
  Check(open("/missing", O_RDONLY) == -1 && errno == ENODEV);
  Check(access("/missing", F_OK) == -1 && errno == ENODEV);
  Check(mkdir("/missing", 0) == -1 && errno == ENODEV);
  Check(unlink("/missing") == -1 && errno == ENODEV);
  Check(rmdir("/missing") == -1 && errno == ENODEV);
#endif
#if defined(GTOS_PATH_GUEST_WRONG_EXPECTATION)
  Check(false);
#endif
  native_path_record.stage = 2;
  Write(native_path_record.failures ? "PATH CONSUMER FAIL\n" : "PATH CONSUMER PASS\n");
  return native_path_record.failures ? 1 : 0;
}
