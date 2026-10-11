#include "clock-gettime-internal.h"
#include <stdint.h>

#if !defined(__GTOS__) || !defined(__i386__) || defined(__linux__) || defined(__unix__)
#error This provider requires the actual GTOS IA32 native clock ABI
#endif
static_assert(sizeof(clockid_t) == 4 && sizeof(time_t) == 8 &&
                  sizeof(timespec) == 16 && sizeof(timespec::tv_nsec) == 8,
              "Actual LLVM C SDK time layout");
static_assert(CLOCK_MONOTONIC == GTOS_CLOCK_ID_MONOTONIC &&
                  CLOCK_REALTIME == GTOS_CLOCK_ID_REALTIME,
              "Private GTOS C clock selectors");

namespace {
int Call(unsigned operation, const void* request, unsigned bytes) {
  int result;
  asm volatile("int $0x80"
               : "=a"(result)
               : "a"(operation), "b"(reinterpret_cast<uintptr_t>(request)),
                 "c"(bytes)
               : "memory", "cc");
  return result;
}
int Fail(int error) {
  errno = error;
  return -1;
}
}  // namespace

extern "C" int clock_gettime(clockid_t clock, timespec* output) noexcept {
  if (output == nullptr) return Fail(EFAULT);
  timespec converted = {};
  int error;
  if (clock == CLOCK_MONOTONIC) {
    GtosClockReadResult value = {};
    GtosClockReadRequest request = {
        GTOS_CLOCK_ABI_VERSION, GTOS_CLOCK_ID_MONOTONIC, 0,
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(&value)), sizeof(value)};
    const int status = Call(GTOS_SYS_CLOCK_READ, &request, sizeof(request));
    error = status ? gtos_clock::Error(status) : gtos_clock::Decode(value, converted);
  } else if (clock == CLOCK_REALTIME) {
    GtosRealtimeReadResult value = {};
    GtosRealtimeReadRequest request = {
        GTOS_REALTIME_ABI_VERSION, 0,
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(&value)), sizeof(value)};
    const int status = Call(GTOS_SYS_REALTIME_READ, &request, sizeof(request));
    error = status ? gtos_clock::Error(status) : gtos_clock::Decode(value, converted);
  } else {
    return Fail(EINVAL);
  }
  if (error) return Fail(error);
  // Invalid nonnull caller storage follows normal C memory access semantics.
  // Native errors and metadata rejection leave the caller's output unchanged.
  *output = converted;
  return 0;
}
