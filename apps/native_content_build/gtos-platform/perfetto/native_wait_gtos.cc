#include "perfetto/ext/base/native_wait.h"
#include "perfetto/base/time.h"
#include <process/abi.h>
#include <process/clock_abi.h>
#include <process/event_abi.h>
#include <stdint.h>

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_GTOS) || !defined(__i386__)
#error Native waits require the GTOS IA32 ABI
#endif
#if defined(GTOS_PERFETTO_WAIT_TEST)
extern "C" int GtosPerfettoWaitTestCall(unsigned, const void*, unsigned);
#endif

namespace perfetto {
namespace base {
namespace {
int Call(unsigned operation, const void* request, unsigned bytes) {
#if defined(GTOS_PERFETTO_WAIT_TEST)
  return GtosPerfettoWaitTestCall(operation, request, bytes);
#else
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(reinterpret_cast<uintptr_t>(request)), "c"(bytes)
               : "memory", "cc");
  return static_cast<int>(operation);
#endif
}
int64_t ElapsedNs(int64_t begin) {
  const int64_t now = GetWallTimeNs().count();
  PERFETTO_CHECK(now >= begin);
  return now - begin;
}
}

size_t WaitForNativeEvents(const PlatformHandle* handles, size_t count,
                           int timeout_ms) {
  PERFETTO_CHECK(handles && count > 0 && count <= GTOS_EVENT_WAIT_LIMIT);
  PERFETTO_CHECK(timeout_ms >= -1);
  for (size_t i = 0; i < count; ++i)
    PERFETTO_CHECK(handles[i] > 0);
  const int64_t duration_ns = static_cast<int64_t>(timeout_ms) * 1000000;
  const int64_t begin = timeout_ms > 0 ? GetWallTimeNs().count() : 0;
  int64_t elapsed_ns = 0;
  for (;;) {
    unsigned ticks = GTOS_EVENT_TIMEOUT_LIMIT;
    if (timeout_ms >= 0) {
      // Divide before multiplying: even INT_MAX milliseconds fits in uint64_t.
      // The canonical PIT divisor is not exactly ten milliseconds. Round up
      // in its actual units, and recheck the clock after a timeout to cover
      // the partially elapsed first tick. Never promise sub-tick precision.
      const uint64_t us = static_cast<uint64_t>(duration_ns - elapsed_ns + 999) / 1000;
      constexpr uint64_t denominator = 1000000ULL * GTOS_CLOCK_PIT_DIVISOR;
      ticks = static_cast<unsigned>((us * GTOS_CLOCK_PIT_INPUT_HZ + denominator - 1) / denominator);
    }
    const GtosEventWaitRequest request{GTOS_EVENT_ABI_VERSION,
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(handles)),
        static_cast<unsigned>(count), ticks};
    const int result = Call(GTOS_SYS_EVENT_WAIT, &request, sizeof(request));
    if (result >= 0) {
      PERFETTO_CHECK(static_cast<size_t>(result) < count);
      return static_cast<size_t>(result);
    }
    PERFETTO_CHECK(result == GTOS_EVENT_ERR_TIMEOUT);
    if (timeout_ms == 0)
      return count;
    if (timeout_ms > 0) {
      elapsed_ns = ElapsedNs(begin);
      if (elapsed_ns >= duration_ns)
        return count;
    }
  }
}

void SleepMicroseconds(unsigned interval_us) {
  if (interval_us == 0)
    return;
  const int64_t duration_ns = static_cast<int64_t>(interval_us) * 1000;
  const int64_t begin = GetWallTimeNs().count();
  // Match the existing native libc++ sleep provider. This yields runnable
  // CPU time until the coarse uptime deadline; it does not claim a precise
  // hardware timer or require an event endpoint in a clock-only process.
  do {
    PERFETTO_CHECK(Call(GTOS_SYS_YIELD, nullptr, 0) == 0);
  } while (ElapsedNs(begin) < duration_ns);
}
}  // namespace base
}  // namespace perfetto
