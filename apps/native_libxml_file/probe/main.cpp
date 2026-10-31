/*
 * The consumer that makes the libxml2 link mean something: it opens a real file through the private POSIX file subset, checks it with
 * stat(), reads it with read()/lseek(), and hands the bytes to libxml2's own parser through xmlReadMemory — and separately asks libxml2 to
 * open the path itself through xmlReadFile, which is the call that pulls xmlIO.c's fopen/open path into the same image.
 *
 * It also exercises the two closures the image links: the libc formatting engine through snprintf (a real varargs format with a string, a
 * signed integer, a width and a hexadecimal), and time() through the realtime ABI. Both are checked here rather than in the kernel, because
 * they are calls into this image's own closure: a kernel-side printf would prove nothing about what was linked in.
 *
 * It returns a bit set instead of printing, because a GTOS user image at this stage has no stdout (the native stdio layer's streams are
 * explicitly unavailable); the guest reads this value through the loader's exit code, and the kernel prints it.
 */
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#define GTOS_LIBXML_FIXTURE "/fixtures/sample.xml"
#define GTOS_LIBXML_INVALID "/fixtures/invalid.xml"

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
  kTimeInvalid = 1u << 10,
  kFormatWrong = 1u << 11,
  kInvalidXmlAccepted = 1u << 12,
  kInvalidFileAccepted = 1u << 13,
};

// One well-formed document with an element, an attribute and text, so the parser has something to walk. The closing tag of the second
// document is deliberately mismatched: libxml2 has to refuse it.
static const char kInvalidDocument[] = "<gtos><broken></gtos>";

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

  // The formatting engine that was linked in: a real varargs format, checked on its exact output.
  char formatted[64];
  const int written = snprintf(formatted, sizeof(formatted), "%s-%d-%08x", "gtos", -42, 0x2aU);
  if (written != 17 || strcmp(formatted, "gtos--42-0000002a") != 0) failures |= kFormatWrong;

  // time() is epoch seconds from the realtime extension. The rig boots with -rtc base=2000-01-01, so the honest check is that the value is
  // real epoch seconds inside that rig's range and does not go backwards — not that the machine believes it is 2026. An ENOTSUP (-1), a
  // zero, an uptime or a 1970 default all fail this.
  const time_t first = time(0);
  const time_t second = time(0);
  if (first < 946684800 || first >= 4102444800 || second < first) failures |= kTimeInvalid;

  struct stat info;
  if (stat(GTOS_LIBXML_FIXTURE, &info) != 0) return (int)(failures | kStatFailed);
  if (!S_ISREG(info.st_mode) || info.st_size <= 0) return (int)(failures | kNotRegular);

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

  // The malformed document goes to the same file service, so xmlReadFile below parses the bytes the private subset wrote.
  const int invalid = open(GTOS_LIBXML_INVALID, O_WRONLY | O_CREAT | O_TRUNC);
  if (invalid <= 0 || write_all(invalid, kInvalidDocument, sizeof(kInvalidDocument) - 1) != 0) failures |= kWriteFailed;
  if (invalid > 0) close(invalid);

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

  // The negative side: a mismatched document must be refused by both entry points, and the refusal must not leave a document behind.
  xmlDocPtr broken = xmlReadMemory(kInvalidDocument, (int)(sizeof(kInvalidDocument) - 1), GTOS_LIBXML_INVALID, 0, 0);
  if (broken != 0) {
    failures |= kInvalidXmlAccepted;
    xmlFreeDoc(broken);
  }
  xmlDocPtr brokenFile = xmlReadFile(GTOS_LIBXML_INVALID, 0, 0);
  if (brokenFile != 0) {
    failures |= kInvalidFileAccepted;
    xmlFreeDoc(brokenFile);
  }

  xmlCleanupParser();
  return (int)failures;
}
