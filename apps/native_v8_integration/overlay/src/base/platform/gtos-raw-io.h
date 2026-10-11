#ifndef V8_BASE_PLATFORM_GTOS_RAW_IO_H_
#define V8_BASE_PLATFORM_GTOS_RAW_IO_H_

#include <stddef.h>
#include <stdint.h>
#include "src/base/platform/gtos-native-api/abi.h"

namespace v8 {
namespace base {
namespace gtos {

// Immediate termination skips user-space destructors and buffered I/O.
// The kernel still owns process, file-handle, page and FP reclamation.
[[noreturn]] inline void ExitImmediately(unsigned status) {
  unsigned call = GTOS_SYS_EXIT;
  asm volatile("int $0x80" : "+a"(call)
               : "b"(status), "c"(0U) : "memory", "cc");
  __builtin_trap();
}

inline void WriteError(const char* data, size_t length) {
  while (length != 0) {
    const unsigned bytes = length > GTOS_NATIVE_WRITE_LIMIT
                               ? GTOS_NATIVE_WRITE_LIMIT
                               : static_cast<unsigned>(length);
    unsigned result = GTOS_SYS_WRITE;
    asm volatile("int $0x80"
                 : "+a"(result)
                 : "b"(static_cast<unsigned>(
                       reinterpret_cast<uintptr_t>(data))), "c"(bytes)
                 : "memory", "cc");
    if (result == 0 || result > bytes) return;
    data += result;
    length -= result;
  }
}

}  // namespace gtos
}  // namespace base
}  // namespace v8

#endif  // V8_BASE_PLATFORM_GTOS_RAW_IO_H_
