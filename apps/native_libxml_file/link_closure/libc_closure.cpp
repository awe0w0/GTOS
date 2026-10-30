/*
 * The libc symbols a libxml2 link on GTOS still lacks, measured rather than guessed.
 *
 * Method: the 26 objects the pinned BUILD.gn compiles were combined with `ld -melf_i386 -r` and their undefined list was compared with what
 * the qualified native FILE library (native-file-coherence-20261012-a/native-c-file-02, LLVM libc's stdio/string/heap for GTOS) and the
 * native printf runtime (native-printf-20261009-a) already define. That comparison left exactly these symbols: strcat, toupper, bsearch,
 * time, fprintf, vfprintf, stdout, stderr, abort, and — from xpath.c alone — ceil, fabs, floor, fmod, log10, pow, exit, snprintf.
 * stdout/stderr/abort/snprintf come from the two existing runtimes; xpath.c's libm needs are not answered here and are why the first real
 * link is the parsing set (see the link stage), not the whole file list.
 *
 * Every function below is real code with a documented limit, not a stub that pretends:
 *   * strcat/bsearch follow their C definitions;
 *   * toupper is the C locale (ASCII case), which is what libxml2's ASCII names need;
 *   * time() is epoch seconds from the realtime extension (GTOS_SYS_REALTIME_READ, 0x4714), which is a validated CMOS UTC calendar plus
 *     delivered PIT time. It deliberately does not use the monotonic clock ABI (0x4712): that one documents itself as "not epoch time",
 *     and returning uptime from time() is exactly the kind of silently wrong answer this file is supposed to avoid. A missing RTC is
 *     ENOTSUP with -1, not 1970 and not uptime;
 *   * fprintf/vfprintf format through the native vsnprintf, then write every produced byte with fwrite. A conversion longer than the
 *     stack buffer is formatted again into a heap buffer of the exact size rather than truncated, and a short write or a null stream is
 *     reported as -1 with errno set instead of returning the byte count as if the text had been written.
 */
#include <process/realtime_abi.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static_assert(sizeof(void *) == 4, "the GTOS file ABI is a 32-bit interface");

// The reduced SDK headers on the probe side carry a subset of errno. These are the standard values, defined only where the header is silent, so
// the same unit compiles against both the libxml SDK and the probe's narrower one and reports the same numbers either way.
#ifndef ENOTSUP
#define ENOTSUP 95
#endif
#ifndef EOVERFLOW
#define EOVERFLOW 75
#endif

// The reduced SDK headers also leave strlen undeclared; its definition is the runtime's, and strcat below is written in terms of it.
extern "C" size_t strlen(const char *);

/*
 * The standard streams. The GTOS stdio layer ships an object that defines the streams as unavailable (its own file name says so), and the
 * C names libxml2's headers reference are not among its definitions. These are those names: null, matching the layer's own statement, and
 * vfprintf above treats a null stream as "nothing to write" rather than crashing on it.
 */
extern "C" FILE *stdin = nullptr;
extern "C" FILE *stdout = nullptr;
extern "C" FILE *stderr = nullptr;

extern "C" char *strcat(char *destination, const char *source) {
  char *end = destination + strlen(destination);
  while ((*end++ = *source++) != 0) {
  }
  return destination;
}

extern "C" char *strncpy(char *destination, const char *source, size_t count) {
  size_t index = 0;
  for (; index < count && source[index] != 0; ++index) destination[index] = source[index];
  for (; index < count; ++index) destination[index] = 0;
  return destination;
}

extern "C" int toupper(int character) {
  if (character >= 'a' && character <= 'z') return character - ('a' - 'A');
  return character;
}

extern "C" void *bsearch(const void *key, const void *base, size_t count, size_t size,
                         int (*compare)(const void *, const void *)) {
  size_t low = 0;
  size_t high = count;
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    const void *element = static_cast<const char *>(base) + middle * size;
    const int order = compare(key, element);
    if (order == 0) return const_cast<void *>(element);
    if (order < 0)
      high = middle;
    else
      low = middle + 1;
  }
  return nullptr;
}

extern "C" time_t time(time_t *output) {
  GtosRealtimeReadResult result{};
  const GtosRealtimeReadRequest request{GTOS_REALTIME_ABI_VERSION, 0, reinterpret_cast<unsigned>(&result), sizeof(result)};
  unsigned status = GTOS_SYS_REALTIME_READ;
  asm volatile("int $0x80" : "+a"(status) : "b"(reinterpret_cast<unsigned>(&request)), "c"(sizeof(request)) : "memory", "cc");
  const bool ok = static_cast<int>(status) == 0 && result.version == GTOS_REALTIME_ABI_VERSION &&
                  result.unit == GTOS_REALTIME_UNIT_MICROSECONDS &&
                  (result.capabilities & GTOS_REALTIME_REQUIRED_CAPABILITIES) == GTOS_REALTIME_REQUIRED_CAPABILITIES;
  if (!ok) {
    // No calendar is a real failure to report, not a zero second: the caller of time() distinguishes "unknown" from "1970".
    errno = static_cast<int>(status) == GTOS_REALTIME_ERR_UNSUPPORTED ? ENOTSUP
            : static_cast<int>(status) == GTOS_REALTIME_ERR_OVERFLOW  ? EOVERFLOW
                                                                      : EIO;
    if (output != nullptr) *output = static_cast<time_t>(-1);
    return static_cast<time_t>(-1);
  }
  const unsigned long long seconds = result.microseconds / 1000000ULL;
  const unsigned long long limit = static_cast<unsigned long long>(static_cast<time_t>(-1) > 0 ? static_cast<time_t>(-1) : 0);
  if (limit != 0 && seconds > limit) {
    errno = EOVERFLOW;
    if (output != nullptr) *output = static_cast<time_t>(-1);
    return static_cast<time_t>(-1);
  }
  const time_t value = static_cast<time_t>(seconds);
  if (output != nullptr) *output = value;
  return value;
}

extern "C" int vfprintf(FILE *stream, const char *format, va_list arguments) {
  // The GTOS stdio layer ships its standard streams as unavailable (the object is named standard_streams_unavailable). A null stream is
  // therefore a real "this went nowhere" and has to be visible to the caller: returning the formatted length would let an error path
  // believe its message was written.
  if (stream == nullptr) {
    errno = EBADF;
    return -1;
  }
  char on_stack[512];
  va_list counted;
  va_copy(counted, arguments);
  const int produced = vsnprintf(on_stack, sizeof(on_stack), format, counted);
  va_end(counted);
  if (produced < 0) {
    errno = EILSEQ;
    return -1;
  }
  const char *text = on_stack;
  char *heap = nullptr;
  size_t bytes = static_cast<size_t>(produced);
  if (static_cast<size_t>(produced) >= sizeof(on_stack)) {
    // Longer than the stack buffer: format the whole conversion instead of truncating it.
    heap = static_cast<char *>(malloc(static_cast<size_t>(produced) + 1));
    if (heap == nullptr) {
      errno = ENOMEM;
      return -1;
    }
    va_list full;
    va_copy(full, arguments);
    const int again = vsnprintf(heap, static_cast<size_t>(produced) + 1, format, full);
    va_end(full);
    if (again < 0 || again > produced) {
      free(heap);
      errno = EILSEQ;
      return -1;
    }
    // The second pass is authoritative: it may be shorter than the first (locale or `%n`-free differences still exist), and writing the first
    // count would put stale bytes of the buffer on the stream.
    bytes = static_cast<size_t>(again);
    text = heap;
  }
  const size_t written = bytes == 0 ? 0 : fwrite(text, 1, bytes, stream);
  free(heap);
  if (written != bytes) {
    errno = EIO;
    return -1;
  }
  return produced;
}

extern "C" int fprintf(FILE *stream, const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  const int produced = vfprintf(stream, format, arguments);
  va_end(arguments);
  return produced;
}
