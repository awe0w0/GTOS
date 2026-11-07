/*
 * The real business consumer of the private POSIX subset, run as the eighth module of the qualified native file kernel.
 *
 * The seven modules before it test the file ABI directly: raw requests, exact trap counts, namespace, boundaries, I/O failures. This one exists
 * for the layer above that: posix_file.cpp's translation of caller flags, path bounds, errno mapping, dup offsets, seek origins and close
 * invalidation. It runs on the same real endpoint — a formatted, mounted, seeded ATA volume on isolated CPL3 — so a pass here means the
 * implementation libxml2's xmlIO.c links against actually performed file I/O, not merely that the raw ABI does.
 *
 * It reports through the same FileRecord the kernel reads: one bit per family in `mask`, every assertion in `checks`, `error` set to the failing
 * check number before exiting 91, and `stage` 2 on success. Nothing is printed, because this kernel's console belongs to the kernel.
 */
#include <process/abi.h>
#include <process/file_abi.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "record.h"

#ifndef EOVERFLOW
#define EOVERFLOW 75
#endif

__attribute__((section(".data.file_record"))) volatile FileRecord native_file_record = {
    1, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// The translation layer reads and writes errno; in the qualified images that accessor comes from the LLVM libc FILE library. This module is
// linked with the probe's default providers only, so it carries the accessor itself — weak, so a real provider wins if one is ever linked.
extern "C" __attribute__((weak)) int *__llvm_libc_errno(void) {
  static int storage;
  return &storage;
}

namespace {

const char kFixture[] = "/fixtures/image.png";
const char kScratch[] = "/native/posix-translation.bin";
const char kPattern[] = "GTOS POSIX TRANSLATION\n";

void raw_exit(unsigned code) {
  unsigned result = GTOS_SYS_EXIT;
  asm volatile("int $0x80" : "+a"(result) : "b"(code) : "memory", "cc");
}

void need(bool value) {
  native_file_record.checks = native_file_record.checks + 1;
  if (!value) {
    native_file_record.error = native_file_record.checks;
    raw_exit(91);
    for (;;) asm volatile("ud2");
  }
}

void family(unsigned bit) { native_file_record.mask = native_file_record.mask | (1U << bit); }

bool equal(const char *left, const char *right, unsigned bytes) {
  for (unsigned index = 0; index < bytes; ++index)
    if (left[index] != right[index]) return false;
  return true;
}

} // namespace

extern "C" void NativeEntry(void) {
  // 1. stat through the translation: the seeded fixture is a regular file of exactly the bytes the kernel wrote.
  struct stat info;
  need(stat(kFixture, &info) == 0 && S_ISREG(info.st_mode) && info.st_size == 1108);
  family(0);

  // 2. read the fixture to its end: a full read, then EOF as zero rather than an error, then a close that succeeds.
  const int fixture = open(kFixture, O_RDONLY);
  need(fixture > 0);
  char buffer[1108];
  need(read(fixture, buffer, sizeof(buffer)) == (ssize_t)sizeof(buffer));
  need(read(fixture, buffer, 16) == 0);
  need(close(fixture) == 0);
  family(1);

  // 3. create, write, close, then stat the result: the size must be what was written, observed through a different call.
  const int created = open(kScratch, O_WRONLY | O_CREAT | O_EXCL);
  need(created > 0);
  const unsigned pattern = sizeof(kPattern) - 1;
  need(write(created, kPattern, pattern) == (ssize_t)pattern);
  need(close(created) == 0);
  struct stat grown;
  need(stat(kScratch, &grown) == 0 && grown.st_size == (off_t)pattern);
  family(2);

  // 4. read it back and compare: the file the ABI stored is the file it returns.
  const int reopened = open(kScratch, O_RDONLY);
  need(reopened > 0);
  char readback[sizeof(kPattern)];
  need(read(reopened, readback, pattern) == (ssize_t)pattern);
  need(equal(readback, kPattern, pattern));
  family(3);

  // 5. seek through the translation: END reports the real size, SET moves the same handle back to the first byte.
  need(lseek(reopened, 0, SEEK_END) == (off_t)pattern);
  need(lseek(reopened, 0, SEEK_SET) == 0);
  char first = 0;
  need(read(reopened, &first, 1) == 1 && first == kPattern[0]);
  family(4);

  // 6. dup shares the offset: the duplicate continues where the original stopped, and closing one leaves the other usable.
  const int duplicate = dup(reopened);
  need(duplicate > 0 && duplicate != reopened);
  char next = 0;
  need(read(duplicate, &next, 1) == 1 && next == kPattern[1]);
  need(gtos_read_at(duplicate, &next, 1, pattern - 1) == 1 && next == kPattern[pattern - 1]);
  need(lseek(reopened, 0, SEEK_CUR) == 2);
  need(gtos_read_at(duplicate, &next, 1, pattern) == 0);
  need(gtos_read_at(duplicate, &next, 1, -1) == -1 && errno == EINVAL);
  need(gtos_read_at(duplicate, &next, 1, (off_t)1 << 31) == -1 && errno == EOVERFLOW);
  need(gtos_write_at(duplicate, "?", 1, 0) == -1 && errno == EBADF);
  need(gtos_read_at(duplicate, reinterpret_cast<void *>(0x10), 1, 0) == -1 && errno == EFAULT);
  need(gtos_read_at(duplicate, nullptr, 0, 0) == 0);
  need(close(duplicate) == 0);
  need(read(reopened, &next, 1) == 1 && next == kPattern[2]);
  need(close(reopened) == 0);
  need(close(duplicate) == -1 && errno == EBADF);
  family(5);

  const int positioned = open(kScratch, O_RDWR);
  need(positioned > 0);
  need(lseek(positioned, 1, SEEK_SET) == 1);
  need(gtos_write_at(positioned, "p", 1, 3) == 1);
  need(lseek(positioned, 0, SEEK_CUR) == 1);
  need(gtos_read_at(positioned, &next, 1, 3) == 1 && next == 'p');
  need(close(positioned) == 0);
  family(9);

  // 7. append: O_APPEND stores at the end, and the size the store reports grows by exactly the appended byte. The size is read after the
  // close, because the store flushes a handle's buffered write on close — reading it before would report the pre-append size and look like
  // the append had been dropped.
  const int appended = open(kScratch, O_WRONLY | O_APPEND);
  need(appended > 0);
  need(lseek(appended, 0, 7) == -1 && errno == EINVAL);
  need(lseek(appended, pattern + 100, SEEK_SET) == (off_t)(pattern + 100));
  need(gtos_write_at(appended, "!", 1, -1) == 1);
  need(lseek(appended, 0, SEEK_CUR) == (off_t)(pattern + 1));
  need(close(appended) == 0);
  struct stat after;
  need(stat(kScratch, &after) == 0 && after.st_size == (off_t)(pattern + 1));
  family(6);

  // 8. the refusals that belong to this layer, not to the store: unknown flags, a null path, the empty path, a bad descriptor, a bad origin.
  need(open(kFixture, O_RDONLY | 0x40000) == -1 && errno == EINVAL);
  need(open(0, O_RDONLY) == -1 && errno == EINVAL);
  need(open("", O_RDONLY) == -1 && errno == ENOENT);
  need(close(0) == -1 && errno == EBADF);
  family(7);

  // 9. the errors that belong to the store: a missing path is ENOENT, and an exclusive create over an existing path is EEXIST.
  need(open("/fixtures/not-here.png", O_RDONLY) == -1 && errno == ENOENT);
  need(open(kScratch, O_WRONLY | O_CREAT | O_EXCL) == -1 && errno == EEXIST);
  family(8);

  native_file_record.stage = 2;
  native_file_record.mask = native_file_record.mask | 0x100;
  raw_exit(0);
  for (;;) asm volatile("ud2");
}
