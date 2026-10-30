/*
 * The file-subset consumer. It uses only the private POSIX calls this unit implements — open, close, read, write, lseek, dup, stat — so its
 * image links without libxml2 and measures the subset itself: that a real i386 GTOS user image can be built from these seven calls over the
 * native file ABI and the qualified FILE library, with no undefined symbol left.
 *
 * The cases are the ones the ABI documents and a libxml2 reader depends on: a regular file is a regular file; a read moves the offset; dup
 * shares that offset; a write through a second handle reaches the same file; and the errors are reported as -1 with errno, not as a
 * positive value that a caller would mistake for a byte count.
 */
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <errno.h>
#include <string.h>

#define GTOS_FILE_FIXTURE "/fixtures/sample.xml"
#define GTOS_FILE_SCRATCH "/fixtures/posix-scratch.bin"

enum {
  kStatFailed = 1u << 0,
  kNotRegular = 1u << 1,
  kEmptyFile = 1u << 2,
  kOpenFailed = 1u << 3,
  kShortRead = 1u << 4,
  kOffsetWrong = 1u << 5,
  kDupFailed = 1u << 6,
  kDupOffsetNotShared = 1u << 7,
  kSeekFailed = 1u << 8,
  kWriteFailed = 1u << 9,
  kReopenFailed = 1u << 10,
  kBytesDiffer = 1u << 11,
  kCloseFailed = 1u << 12,
  kOpenMissingSucceeded = 1u << 13,
  kErrnoNotSet = 1u << 14,
};

static unsigned read_all(int fd, char *buffer, unsigned bytes) {
  unsigned done = 0;
  while (done < bytes) {
    const ssize_t got = read(fd, buffer + done, bytes - done);
    if (got <= 0) break;
    done += (unsigned)got;
  }
  return done;
}

extern "C" int FileSubsetEntry(void) {
  unsigned failures = 0;
  struct stat info;
  if (stat(GTOS_FILE_FIXTURE, &info) != 0) return kStatFailed;
  if (!S_ISREG(info.st_mode)) return kNotRegular;
  if (info.st_size <= 0) return kEmptyFile;

  const int fd = open(GTOS_FILE_FIXTURE, O_RDONLY);
  if (fd <= 0) return kOpenFailed;

  // A 16-byte read must leave the offset at 16, and a duplicate must observe that same offset: the ABI shares it, and if it did not, every
  // buffered reader built on top would silently read the wrong bytes after a dup.
  char head[16];
  if (read(fd, head, sizeof(head)) != (ssize_t)sizeof(head)) failures |= kShortRead;
  if (lseek(fd, 0, SEEK_CUR) != (off_t)sizeof(head)) failures |= kOffsetWrong;

  const int copy = dup(fd);
  if (copy <= 0) {
    failures |= kDupFailed;
  } else {
    if (lseek(copy, 0, SEEK_CUR) != (off_t)sizeof(head)) failures |= kDupOffsetNotShared;
    // Seeking through the duplicate moves the shared offset, so the original must observe it too.
    if (lseek(copy, 0, SEEK_SET) != 0) failures |= kSeekFailed;
    if (lseek(fd, 0, SEEK_CUR) != 0) failures |= kSeekFailed;
    close(copy);
  }

  static char document[4096];
  const unsigned loaded = read_all(fd, document, sizeof(document) - 1);
  if (loaded == 0) failures |= kShortRead;
  close(fd);

  // The same bytes through a second handle, then back: this is the read/write pair that a save path needs.
  const int scratch = open(GTOS_FILE_SCRATCH, O_WRONLY | O_CREAT | O_TRUNC);
  if (scratch <= 0) {
    failures |= kWriteFailed;
  } else {
    unsigned done = 0;
    while (done < loaded) {
      const ssize_t put = write(scratch, document + done, loaded - done);
      if (put <= 0) {
        failures |= kWriteFailed;
        break;
      }
      done += (unsigned)put;
    }
    if (close(scratch) != 0) failures |= kCloseFailed;
  }

  const int reread = open(GTOS_FILE_SCRATCH, O_RDONLY);
  if (reread <= 0) {
    failures |= kReopenFailed;
  } else {
    static char again[4096];
    const unsigned got = read_all(reread, again, sizeof(again) - 1);
    if (got != loaded || memcmp(document, again, loaded) != 0) failures |= kBytesDiffer;
    close(reread);
  }

  // A missing file must fail with -1 and a set errno, not with a positive handle.
  errno = 0;
  if (open("/fixtures/not-present.xml", O_RDONLY) >= 0) failures |= kOpenMissingSucceeded;
  if (errno == 0) failures |= kErrnoNotSet;

  return (int)failures;
}
