#include "nanosleep-internal.h"
#include "clock-gettime-internal.h"
#include "src/base/platform/gtos-native-api/abi.h"
#include <stdint.h>

#if !defined(__GTOS__) || !defined(__i386__) || defined(__linux__) || defined(__unix__)
#error This provider requires the actual GTOS IA32 native clock and yield ABI
#endif
static_assert(sizeof(time_t) == 8 && sizeof(timespec) == 16 &&
                  sizeof(timespec::tv_nsec) == 8,
              "Actual LLVM C SDK time layout");

namespace {
int Fail(int error) {
  errno = error;
  return -1;
}
int Yield() {
  int result;
  asm volatile("int $0x80"
               : "=a"(result)
               : "a"(GTOS_SYS_YIELD), "b"(0U), "c"(0U)
               : "memory", "cc");
  return result;
}
}  // namespace

extern "C" int nanosleep(const timespec* requested, timespec*) noexcept {
  if (requested == nullptr) return Fail(EFAULT);
  // A remainder may alias the request. There are no native user signals in
  // ABI1, so no EINTR remainder is produced; successful calls leave it alone.
  // Nonnull request storage follows the normal C readable-object precondition.
  const timespec duration = *requested;
  if (!gtos_sleep::Valid(duration)) return Fail(EINVAL);
  if (duration.tv_sec == 0 && duration.tv_nsec == 0) return 0;
  timespec begin = {};
  if (clock_gettime(CLOCK_MONOTONIC, &begin) != 0) return -1;
  for (;;) {
    const int status = Yield();
    if (status != 0) return Fail(gtos_clock::Error(status));
    timespec now = {};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    const int elapsed = gtos_sleep::Elapsed(begin, now, duration);
    if (elapsed < 0) return Fail(EIO);
    if (elapsed != 0) return 0;
  }
}
