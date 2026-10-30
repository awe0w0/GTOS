/*
 * Does the GTOS file layer actually read and write, in the guest?
 *
 * file_probe.cpp answers that for a file that already exists — the guest has no such fixture, so it stops at stat() with code 1. This consumer needs
 * nothing preinstalled: it writes a known pattern through the private POSIX subset (open O_CREAT|O_TRUNC, write, close), reads it back through a
 * second handle, compares, and reports. Every step goes through the same posix_file.cpp translation the libxml2 build linked against, so a pass
 * here is the file layer working on GTOS, not a host simulation.
 *
 * It reports through the platform's own message call (0x4701) so the result lands in the guest log, and returns a bitmask for the exit code. A
 * failure at any step is named in the log with its step letter, because "exit 5" alone would not say which call failed.
 */
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <errno.h>

namespace {

const char kScratchPath[] = "/file-subset-probe.bin";
const char kPattern[] = "GTOS FILE SUBSET: write, read, compare, close.";

int message(const char *text, unsigned length) {
  int result;
  asm volatile("int $0x80" : "=a"(result) : "a"(0x4701U), "b"((unsigned)text), "c"(length) : "memory", "cc");
  return result;
}

void report(const char *text) {
  unsigned length = 0;
  while (text[length] != 0) ++length;
  message(text, length);
}

} // namespace

extern "C" int NativeEntry(void) {
  report("GTOS FILE SUBSET BEGIN\n");

  const unsigned patternBytes = sizeof(kPattern) - 1;
  // The exit code carries the step in its low bits and the errno the ABI reported in the next byte: "1" alone would not distinguish "no writable
  // volume" from "the flags were rejected", and those two need different work.
  const int writer = open(kScratchPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (writer < 0) {
    report("GTOS FILE SUBSET FAIL open-for-write\n");
    return 1 | ((errno & 0xFF) << 8);
  }
  unsigned written = 0;
  while (written < patternBytes) {
    const ssize_t put = write(writer, kPattern + written, patternBytes - written);
    if (put <= 0) {
      report("GTOS FILE SUBSET FAIL write\n");
      close(writer);
      return 2;
    }
    written += (unsigned)put;
  }
  if (close(writer) != 0) {
    report("GTOS FILE SUBSET FAIL close-after-write\n");
    return 4;
  }

  struct stat info;
  if (stat(kScratchPath, &info) != 0) {
    report("GTOS FILE SUBSET FAIL stat-after-write\n");
    return 8 | ((errno & 0xFF) << 8);
  }
  if (!S_ISREG(info.st_mode) || info.st_size != (off_t)patternBytes) {
    report("GTOS FILE SUBSET FAIL size-or-type\n");
    return 16;
  }

  const int reader = open(kScratchPath, O_RDONLY);
  if (reader < 0) {
    report("GTOS FILE SUBSET FAIL open-for-read\n");
    return 32 | ((errno & 0xFF) << 8);
  }
  char readback[sizeof(kPattern)];
  unsigned loaded = 0;
  while (loaded < patternBytes) {
    const ssize_t got = read(reader, readback + loaded, patternBytes - loaded);
    if (got <= 0) break;
    loaded += (unsigned)got;
  }
  close(reader);

  if (loaded != patternBytes) {
    report("GTOS FILE SUBSET FAIL short-read\n");
    return 64;
  }
  for (unsigned index = 0; index < patternBytes; ++index) {
    if (readback[index] != kPattern[index]) {
      report("GTOS FILE SUBSET FAIL bytes-differ\n");
      return 128;
    }
  }

  // The offset the second handle left behind must be the file size, not the size of the last read.
  const int check = open(kScratchPath, O_RDONLY);
  if (check < 0 || lseek(check, 0, SEEK_END) != (off_t)patternBytes) {
    report("GTOS FILE SUBSET FAIL seek-end\n");
    if (check >= 0) close(check);
    return 256;
  }
  close(check);

  report("GTOS FILE SUBSET PASS write-read-compare\n");
  return 0;
}
