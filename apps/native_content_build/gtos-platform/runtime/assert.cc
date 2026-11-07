// Assertion failures in the private GTOS C runtime.
#include <assert.h>
#include <stdlib.h>

#include <process/abi.h>

#if !defined(__GTOS__) || !defined(__i386__)
#error Native assertions require the GTOS IA32 ABI.
#endif

namespace {
void Append(char*& out, const char* text, unsigned limit) {
  if (!text)
    return;
  for (; limit && *text; --limit)
    *out++ = *text++;
}
}  // namespace

extern "C" [[noreturn]] void __assert_fail(const char* condition,
                                          const char* file,
                                          unsigned line,
                                          const char* function) noexcept {
  char buffer[256];
  char* out = buffer;
  Append(out, "GTOS assertion: ", 16);
  Append(out, condition, 80);
  Append(out, " at ", 4);
  const char* filename = file;
  for (const char* part = file; part && *part; ++part) {
    if (*part == '/' || *part == '\\')
      filename = part + 1;
  }
  Append(out, filename, 100);
  *out++ = ':';
  char digits[10];
  unsigned count = 0;
  do {
    digits[count++] = static_cast<char>('0' + line % 10);
    line /= 10;
  } while (line);
  while (count)
    *out++ = digits[--count];
  Append(out, " (", 2);
  Append(out, function, 40);
  *out++ = ')';
  *out++ = '\n';
  unsigned call = GTOS_SYS_WRITE;
  asm volatile("int $0x80"
               : "+a"(call)
               : "b"(buffer), "c"(static_cast<unsigned>(out - buffer))
               : "memory", "cc");
  abort();
}
