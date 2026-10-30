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
 *   * time() cannot be epoch time on GTOS: the clock ABI offers only a coarse monotonic uptime and says so ("not epoch time"), so this
 *     returns that uptime and is documented as such instead of inventing a date;
 *   * fprintf/vfprintf format through the native vsnprintf and write the bytes with fwrite, bounded by one 512-byte buffer — a bigger
 *     conversion is truncated and the return value says how much was produced, not how much was written.
 */
#include <process/clock_abi.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static_assert(sizeof(void *) == 4, "the GTOS file ABI is a 32-bit interface");

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
  GtosClockReadResult result{};
  const GtosClockReadRequest request{GTOS_CLOCK_ABI_VERSION, GTOS_CLOCK_ID_MONOTONIC, 0,
                                     reinterpret_cast<unsigned>(&result), sizeof(result)};
  unsigned status = GTOS_SYS_CLOCK_READ;
  asm volatile("int $0x80" : "+a"(status) : "b"(reinterpret_cast<unsigned>(&request)), "c"(sizeof(request)) : "memory", "cc");
  time_t seconds = 0;
  if (static_cast<int>(status) == 0 && result.version == GTOS_CLOCK_ABI_VERSION) {
    seconds = static_cast<time_t>(result.microseconds / 1000000ULL);
  }
  if (output != nullptr) *output = seconds;
  return seconds;
}

extern "C" int vfprintf(FILE *stream, const char *format, va_list arguments) {
  char buffer[512];
  const int produced = vsnprintf(buffer, sizeof(buffer), format, arguments);
  // The GTOS stdio layer marks the standard streams unavailable (its own object is named that). A null stream here means "nowhere to
  // write": the text is formatted and dropped instead of dereferencing null, so an error path that logs cannot take the process down.
  if (stream == nullptr) return produced;
  if (produced > 0) {
    const size_t counted = static_cast<size_t>(produced) < sizeof(buffer) ? static_cast<size_t>(produced) : sizeof(buffer) - 1;
    fwrite(buffer, 1, counted, stream);
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
