/*
 * The consumer that makes the libxml2 link mean something.
 *
 * What it does, in the order a reader should check it:
 *   - the file service, the way a program uses it: stat(), open(), read()/lseek()/dup(), write() with a full read-back, close() results
 *     checked rather than dropped;
 *   - the formatting closure: a real varargs format checked on its exact output, a payload longer than the transfer limit, and the
 *     truncation the C standard requires (the return value is the length the output would have had);
 *   - time(): two entry points (time(0) and time(&t)) must agree with each other and with the rig's RTC base, which the builder passes
 *     in as GTOS_RTC_EPOCH from the same command line that boots the guest;
 *   - libxml2 on the bytes the subset read: xmlReadMemory and xmlReadFile, the root's name, an attribute and the text of each item, so a
 *     parser that returns a document without walking it cannot pass;
 *   - the negative side, with the two refusals kept apart: a malformed document must be refused by the parser (the error code says so),
 *     and a path that does not exist must be refused without a parse error, because that is the I/O failing rather than the document.
 *
 * It returns a bit set instead of printing, because a GTOS user image at this stage has no stdout (the native stdio layer's streams are
 * explicitly unavailable); the guest reads this value through the loader's exit code, and the kernel prints it.
 */
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlerror.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef GTOS_RTC_EPOCH
#error "GTOS_RTC_EPOCH must name the rig's RTC base; the range check below is a comparison with it, not with a guessed date"
#endif

#define GTOS_LIBXML_FIXTURE "/fixtures/sample.xml"
#define GTOS_LIBXML_INVALID "/fixtures/invalid.xml"
#define GTOS_LIBXML_SCRATCH "/fixtures/libxml-scratch.xml"
#define GTOS_LIBXML_ABSENT "/fixtures/no-such-file.xml"
#define GTOS_LIBXML_FORMATTED "/fixtures/formatted.txt"

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
  kReadBackMismatch = 1u << 14,
  kCloseFailed = 1u << 15,
  kWrongTree = 1u << 16,
  kInvalidNotParser = 1u << 17,
  kAbsentAccepted = 1u << 18,
  kTimeDisagrees = 1u << 19,
  kLongFormatWrong = 1u << 20,
  kStreamFormatWrong = 1u << 21,
  kNullStreamAccepted = 1u << 22,
  kRefusedWriteAccepted = 1u << 23,
  kEnomemAccepted = 1u << 24,
};

// One well-formed document with two elements, an attribute and text. The closing tag of the second document is deliberately mismatched:
// libxml2 has to refuse it, and has to say why.
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

// The text of an element, compared as bytes. Returns 0 when it differs.
static int text_is(xmlNodePtr node, const char *expected) {
  if (node == 0) return 0;
  xmlChar *content = xmlNodeGetContent(node);
  if (content == 0) return 0;
  const int same = strcmp((const char *)content, expected) == 0;
  xmlFree(content);
  return same;
}

static int attr_is(xmlNodePtr node, const char *name, const char *expected) {
  if (node == 0) return 0;
  xmlChar *value = xmlGetProp(node, (const xmlChar *)name);
  if (value == 0) return 0;
  const int same = strcmp((const char *)value, expected) == 0;
  xmlFree(value);
  return same;
}

// The stream side of the formatting closure, on the FILE the FILE library opens. Every failure branch the closure documents is reached
// here on purpose, and each one has to leave the stream usable afterwards.
static unsigned stream_failures(const char *payload, unsigned payloadBytes) {
  unsigned failures = 0;

  // A conversion longer than the stack buffer: the closure has to format the whole thing into its own allocation and write every byte.
  FILE *out = fopen(GTOS_LIBXML_FORMATTED, "w");
  if (out == 0) return kStreamFormatWrong;
  const int produced = fprintf(out, "%s", payload);
  if (produced != (int)payloadBytes || fflush(out) != 0 || fclose(out) != 0) failures |= kStreamFormatWrong;
  static char readback[4400];
  const int again = open(GTOS_LIBXML_FORMATTED, O_RDONLY);
  if (again <= 0) {
    failures |= kStreamFormatWrong;
  } else {
    const unsigned got = read_all(again, readback, sizeof(readback) - 1);
    if (got != payloadBytes || memcmp(readback, payload, payloadBytes) != 0) failures |= kStreamFormatWrong;
    if (close(again) != 0) failures |= kCloseFailed;
  }

  // A null stream is not a silent success: the caller has to be able to tell that the message went nowhere.
  errno = 0;
  if (fprintf(0, "%s", "nowhere") != -1 || errno != EBADF) failures |= kNullStreamAccepted;

  // A stream opened for reading refuses the write, which is the short-write branch of vfprintf (the byte count does not match).
  FILE *reader = fopen(GTOS_LIBXML_FIXTURE, "r");
  if (reader == 0) {
    failures |= kRefusedWriteAccepted;
  } else {
    errno = 0;
    if (fwrite("x", 1, 1, reader) != 0) failures |= kRefusedWriteAccepted;
    if (fprintf(reader, "%s", "nope") != -1) failures |= kRefusedWriteAccepted;
    if (fclose(reader) != 0) failures |= kCloseFailed;
  }

  // A conversion whose formatted length is larger than the guest heap: the closure's allocation fails, and it says so instead of writing a
  // truncated line. The stream must still work afterwards, which is the recovery half of the same check.
  FILE *wide = fopen(GTOS_LIBXML_FORMATTED, "w");
  if (wide == 0) return failures | kEnomemAccepted;
  errno = 0;
  const int tooBig = fprintf(wide, "%*s", 1 << 20, "");
  if (tooBig != -1 || errno != ENOMEM) failures |= kEnomemAccepted;
  const int recovered = fprintf(wide, "%s", "recovered");
  if (recovered != 9 || fclose(wide) != 0) failures |= kEnomemAccepted;
  const int after = open(GTOS_LIBXML_FORMATTED, O_RDONLY);
  if (after > 0) {
    char tail[32];
    const unsigned got = read_all(after, tail, sizeof(tail) - 1);
    tail[got] = 0;
    if (got != 9 || strcmp(tail, "recovered") != 0) failures |= kEnomemAccepted;
    if (close(after) != 0) failures |= kCloseFailed;
  } else {
    failures |= kEnomemAccepted;
  }
  return failures;
}

extern "C" int NativeEntry(void) {
  unsigned failures = 0;

  // The formatting closure, on its exact output.
  char formatted[64];
  const int written = snprintf(formatted, sizeof(formatted), "%s-%d-%08x", "gtos", -42, 0x2aU);
  if (written != 17 || strcmp(formatted, "gtos--42-0000002a") != 0) failures |= kFormatWrong;

  // A payload longer than the ABI's 4096-byte transfer limit, formatted whole and then formatted into a buffer too small for it. The
  // standard's answer to truncation is specific: the buffer is filled and terminated, and the return value is what would have been
  // written, so a formatter that returns the truncated length fails here.
  static char longPayload[4200];
  static char longOutput[4400];
  char small[100];
  for (unsigned i = 0; i < sizeof(longPayload) - 1; ++i) longPayload[i] = (char)('a' + (i % 26));
  longPayload[sizeof(longPayload) - 1] = 0;
  const int longWritten = snprintf(longOutput, sizeof(longOutput), "%s", longPayload);
  const int smallWritten = snprintf(small, sizeof(small), "%s", longPayload);
  const unsigned longBytes = (unsigned)(sizeof(longPayload) - 1);
  if (longWritten != (int)longBytes || strcmp(longOutput, longPayload) != 0) failures |= kLongFormatWrong;
  if (smallWritten != (int)longBytes || small[sizeof(small) - 1] != 0 || strlen(small) != sizeof(small) - 1) failures |= kLongFormatWrong;
  failures |= stream_failures(longPayload, longBytes);

  // time() against this rig's own base: the builder passes the epoch of the -rtc base it boots with, and one boot cannot move the clock
  // by more than a day. Both entry points have to agree, because they are two paths through the same closure.
  time_t fromCall = 0;
  const time_t first = time(0);
  const time_t second = time(&fromCall);
  const time_t difference = first > second ? first - second : second - first;
  const time_t fromBase = first > (time_t)GTOS_RTC_EPOCH ? first - (time_t)GTOS_RTC_EPOCH : (time_t)GTOS_RTC_EPOCH - first;
  if (first < (time_t)GTOS_RTC_EPOCH || fromBase > 86400) failures |= kTimeInvalid;
  if (difference > 1 || second != fromCall) failures |= kTimeDisagrees;

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
  if (copy > 0 && close(copy) != 0) failures |= kCloseFailed;

  // A second handle over the same file, written through the subset and then read back: the bytes the parser is given below have to be
  // the bytes this program wrote, or "the write reached the service" would be an assumption.
  const int scratch = open(GTOS_LIBXML_SCRATCH, O_WRONLY | O_CREAT | O_TRUNC);
  if (scratch <= 0 || write_all(scratch, document, loaded) != 0) failures |= kWriteFailed;
  if (scratch > 0 && close(scratch) != 0) failures |= kCloseFailed;
  if (scratch > 0) {
    static char readback[4096];
    const int again = open(GTOS_LIBXML_SCRATCH, O_RDONLY);
    if (again <= 0) {
      failures |= kReadBackMismatch;
    } else {
      const unsigned got = read_all(again, readback, sizeof(readback) - 1);
      if (got != loaded || memcmp(readback, document, loaded) != 0) failures |= kReadBackMismatch;
      if (close(again) != 0) failures |= kCloseFailed;
    }
  }
  if (close(fd) != 0) failures |= kCloseFailed;

  // The malformed document goes to the same file service, so xmlReadFile below parses the bytes the private subset wrote.
  const int invalid = open(GTOS_LIBXML_INVALID, O_WRONLY | O_CREAT | O_TRUNC);
  if (invalid <= 0 || write_all(invalid, kInvalidDocument, sizeof(kInvalidDocument) - 1) != 0) failures |= kWriteFailed;
  if (invalid > 0 && close(invalid) != 0) failures |= kCloseFailed;

  xmlDocPtr memory = xmlReadMemory(document, (int)loaded, GTOS_LIBXML_FIXTURE, 0, 0);
  if (memory == 0) failures |= kMemoryParseFailed;

  xmlDocPtr file = xmlReadFile(GTOS_LIBXML_FIXTURE, 0, 0);
  if (file == 0) failures |= kFileParseFailed;

  // The tree, not just its name: the first item carries id="1" and the text "text", the second carries id="2" and no text. A parser that
  // returns a document without building the tree fails here.
  if (memory != 0) {
    xmlNodePtr root = xmlDocGetRootElement(memory);
    if (root == 0 || root->name == 0 || strcmp((const char *)root->name, "gtos") != 0) failures |= kWrongRoot;
    xmlNodePtr firstItem = root != 0 ? root->children : 0;
    while (firstItem != 0 && firstItem->type != XML_ELEMENT_NODE) firstItem = firstItem->next;
    xmlNodePtr secondItem = firstItem != 0 ? firstItem->next : 0;
    while (secondItem != 0 && secondItem->type != XML_ELEMENT_NODE) secondItem = secondItem->next;
    if (!attr_is(firstItem, "id", "1") || !text_is(firstItem, "text")) failures |= kWrongTree;
    if (!attr_is(secondItem, "id", "2") || !text_is(secondItem, "")) failures |= kWrongTree;
    xmlFreeDoc(memory);
  }
  if (file != 0) xmlFreeDoc(file);

  // The negative side, with the two refusals kept apart. A malformed document is refused by the parser, and the parser says so: the last
  // error is the mismatched tag. A path that does not exist is refused too, but not by the parser's verdict on a document — there is no
  // document. Treating the second as the first would let an I/O failure look like a successful rejection.
  xmlResetLastError();
  xmlDocPtr broken = xmlReadMemory(kInvalidDocument, (int)(sizeof(kInvalidDocument) - 1), GTOS_LIBXML_INVALID, 0, 0);
  if (broken != 0) {
    failures |= kInvalidXmlAccepted;
    xmlFreeDoc(broken);
  }
  const xmlError *parseError = xmlGetLastError();
  if (parseError == 0 || parseError->code != XML_ERR_TAG_NAME_MISMATCH) failures |= kInvalidNotParser;

  xmlResetLastError();
  xmlDocPtr brokenFile = xmlReadFile(GTOS_LIBXML_INVALID, 0, 0);
  if (brokenFile != 0) {
    failures |= kInvalidFileAccepted;
    xmlFreeDoc(brokenFile);
  }

  xmlResetLastError();
  xmlDocPtr absent = xmlReadFile(GTOS_LIBXML_ABSENT, 0, 0);
  if (absent != 0) {
    failures |= kAbsentAccepted;
    xmlFreeDoc(absent);
  }
  const xmlError *ioError = xmlGetLastError();
  if (ioError != 0 && ioError->code == XML_ERR_TAG_NAME_MISMATCH) failures |= kInvalidNotParser;

  xmlCleanupParser();
  return (int)failures;
}
