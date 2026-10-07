#ifndef GTOS_COMMON_BOOT_LOG_H
#define GTOS_COMMON_BOOT_LOG_H
#include <common/types.h>
namespace gtos { namespace common {
// BSP diagnostic text only. No heap, device IO or persistence. Readers preserve
// the caller's interrupt state; this is not an SMP or untrusted-memory API.
class BootLog {
  public:
    enum { Capacity = 8192 };
    static void Reset();
    // NUL is ignored so returned lines remain ordinary bounded C strings.
    static void Put(char character);
    static uint32_t Bytes();
    static uint32_t Dropped(); // Saturating count of overwritten bytes.
    static uint32_t Lines();
    // Index zero is the oldest retained line (possibly a truncated prefix).
    // A trailing nonempty partial line counts. Capacity includes the NUL byte.
    // A valid line returns true even when truncated to fit the destination.
    static bool ReadLine(uint32_t oldestLineIndex, char* destination, uint32_t capacity);
};
} }
#endif
