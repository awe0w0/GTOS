// Assertion failures in the private GTOS C runtime.
#include <assert.h>
#include <stdlib.h>

#include <array>
#include <string_view>

#include <process/abi.h>

#if !defined(__GTOS__) || !defined(__i386__)
#error Native assertions require the GTOS IA32 ABI.
#endif

namespace {
using Message = std::array<char, 256>;

void Append(Message& message, unsigned& length, std::string_view text) {
  for (char c : text.substr(0, message.size() - length))
    message[length++] = c;
}
}  // namespace

extern "C" [[noreturn]] void __assert_fail(const char* condition,
                                          const char* file,
                                          unsigned line,
                                          const char* function) noexcept {
  Message message;
  unsigned length = 0;
  Append(message, length, "GTOS assertion: ");
  if (condition)
    Append(message, length, std::string_view(condition).substr(0, 80));
  Append(message, length, " at ");
  if (file) {
    std::string_view filename(file);
    const auto separator = filename.find_last_of("/\\");
    if (separator != std::string_view::npos)
      filename.remove_prefix(separator + 1);
    Append(message, length, filename.substr(0, 100));
  }
  Append(message, length, ":");
  std::array<char, 10> digits;
  unsigned count = 0;
  do {
    digits[count++] = static_cast<char>('0' + line % 10);
    line /= 10;
  } while (line);
  while (count)
    message[length++] = digits[--count];
  Append(message, length, " (");
  if (function)
    Append(message, length, std::string_view(function).substr(0, 40));
  Append(message, length, ")\n");
  unsigned call = GTOS_SYS_WRITE;
  asm volatile("int $0x80"
               : "+a"(call)
               : "b"(message.data()), "c"(length)
               : "memory", "cc");
  abort();
}
