#ifndef V8_BASE_PLATFORM_GTOS_ENTROPY_H_
#define V8_BASE_PLATFORM_GTOS_ENTROPY_H_

#include <stddef.h>
#include <stdint.h>

#include "src/base/platform/gtos-native-services.h"

namespace v8 {
namespace base {
namespace gtos {

inline bool HasHardwareEntropy() {
  uint32_t a, b, c, d;
  asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
               : "a"(0U), "c"(0U));
  if (a < 1) return false;
  asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
               : "a"(1U), "c"(0U));
  return (c & (1U << 30)) != 0;
}

// CPL3 CPU-backed entropy; no time/seed fallback. A failed read is unusable,
// including any prefix already written. Unmapped/RO user pages fault normally.
inline bool ReadHardwareEntropy(void* buffer, size_t length) {
  if (length == 0) return true;
  const uintptr_t begin = reinterpret_cast<uintptr_t>(buffer);
  if (buffer == nullptr || length > UINTPTR_MAX - begin ||
      !HasHardwareEntropy()) return false;
  const GtosProcessInfoResult info = CurrentProcessInfo();
  if (begin < info.user_begin || begin >= info.user_end ||
      length > info.user_end - begin) return false;
  uint8_t* data = static_cast<uint8_t*>(buffer);
  while (length != 0) {
    uint32_t word = 0;
    uint8_t available = 0;
    for (unsigned attempt = 0; attempt != 10 && !available; ++attempt) {
      asm volatile("rdrand %0; setc %1" : "=r"(word), "=qm"(available)
                   : : "cc");
    }
    if (!available) return false;
    const size_t bytes = length < sizeof(word) ? length : sizeof(word);
    for (size_t i = 0; i != bytes; ++i) {
      data[i] = static_cast<uint8_t>(word >> (8 * i));
    }
    data += bytes;
    length -= bytes;
  }
  return true;
}

}  // namespace gtos
}  // namespace base
}  // namespace v8

#endif  // V8_BASE_PLATFORM_GTOS_ENTROPY_H_
