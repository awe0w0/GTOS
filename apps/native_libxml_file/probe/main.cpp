/*
 * The consumer that makes the libxml2 link mean something: it opens a real file through the private POSIX file subset, checks it with
 * stat(), reads it with read()/lseek(), and hands the bytes to libxml2's own parser through xmlReadMemory — and separately asks libxml2 to
 * open the path itself through xmlReadFile, which is the call that pulls xmlIO.c's fopen/open path into the same image.
 *
 * It returns a bit set instead of printing, because a GTOS user image at this stage has no stdout (the native stdio layer's streams are
 * explicitly unavailable); the link stage is the measurement, and a guest run would read this value through the loader's exit code.
 */
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string.h>

#define GTOS_LIBXML_FIXTURE "/fixtures/sample.xml"

enum {
  kStatFailed = 1u << 0,
  kNotRegular = 1u << 1,
  kOpenFailed = 1u << 2,
  kShortRead = 1u << 3,
  kSeekFailed = 1u << 4,
  kDupFailed = 1u << 5,
  kWriteFailed = 1u << 6,
  kMemoryParseFailed = 1u << 7,
  kFileParseFailed = 1u << 8,
  kWrongRoot = 1u << 9,
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

static int write_all(int fd, const char *buffer, unsigned bytes) {
  unsigned done = 0;
  while (done < bytes) {
    const ssize_t put = write(fd, buffer + done, bytes - done);
    if (put <= 0) return -1;
    done += (unsigned)put;
  }
  return 0;
}

extern "C" int NativeEntry(void) {
  unsigned failures = 0;
  struct stat info;
  if (stat(GTOS_LIBXML_FIXTURE, &info) != 0) return (int)kStatFailed;
  if (!S_ISREG(info.st_mode) || info.st_size <= 0) return (int)kNotRegular;

  const int fd = open(GTOS_LIBXML_FIXTURE, O_RDONLY);
  if (fd <= 0) return (int)(failures | kOpenFailed);

  static char document[4096];
  const unsigned loaded = read_all(fd, document, sizeof(document) - 1);
  if (loaded == 0) failures |= kShortRead;
  document[loaded] = 0;

  // The file offset must be where the read left it, and dup() must share that offset: both are ABI guarantees the shim relies on.
  const int copy = dup(fd);
  if (copy <= 0) failures |= kDupFailed;
  if (lseek(fd, 0, SEEK_SET) != 0 || lseek(copy, 0, SEEK_CUR) != 0) failures |= kSeekFailed;
  if (copy > 0) close(copy);

  // A second handle over the same file, written through the subset, proves write() reaches the same service the read came from.
  const int scratch = open("/fixtures/libxml-scratch.xml", O_WRONLY | O_CREAT | O_TRUNC);
  if (scratch <= 0 || write_all(scratch, document, loaded) != 0) failures |= kWriteFailed;
  if (scratch > 0) close(scratch);
  close(fd);

  xmlDocPtr memory = xmlReadMemory(document, (int)loaded, GTOS_LIBXML_FIXTURE, 0, 0);
  if (memory == 0) failures |= kMemoryParseFailed;

  xmlDocPtr file = xmlReadFile(GTOS_LIBXML_FIXTURE, 0, 0);
  if (file == 0) failures |= kFileParseFailed;

  if (memory != 0) {
    xmlNodePtr root = xmlDocGetRootElement(memory);
    if (root == 0 || root->name == 0 || strcmp((const char *)root->name, "gtos") != 0) failures |= kWrongRoot;
    xmlFreeDoc(memory);
  }
  if (file != 0) xmlFreeDoc(file);
  xmlCleanupParser();
  return (int)failures;
}
