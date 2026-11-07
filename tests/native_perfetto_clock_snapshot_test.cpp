#include "perfetto/base/logging.h"
#include "perfetto/ext/base/clock_snapshots.h"
#include "protos/perfetto/common/builtin_clock.pbzero.h"

#include <new>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

namespace {
alignas(16) unsigned char arena[4096];
size_t used;
unsigned mode, uptime_calls, realtime_calls;

[[noreturn]] void Finish(int status) {
  asm volatile("int $0x80" : : "a"(1), "b"(status) : "memory", "cc");
  __builtin_trap();
}
}  // namespace

void* operator new(size_t bytes) {
  if (bytes > sizeof(arena) - used)
    Finish(71);
  void* result = arena + used;
  used += (bytes + 15) & ~size_t{15};
  return result;
}
void operator delete(void*) noexcept {}
void operator delete(void*, size_t) noexcept {}
void* operator new(size_t bytes, std::align_val_t alignment) {
  if (static_cast<size_t>(alignment) > 16)
    Finish(71);
  return ::operator new(bytes);
}
void operator delete(void*, size_t, std::align_val_t) noexcept {}

extern "C" int clock_gettime(clockid_t clock, timespec* value) noexcept {
  if (clock == CLOCK_MONOTONIC) {
    ++uptime_calls;
    *value = {12, 345000};
    return 0;
  }
  if (clock == CLOCK_REALTIME) {
    ++realtime_calls;
    *value = {mode == 2 ? INT64_MAX : 946684800, 9000000};
    return mode == 1 ? -1 : 0;
  }
  Finish(72);
}

namespace perfetto::base {
void LogMessage(LogLev, const char*, int, const char*, ...) { Finish(73); }
#if PERFETTO_ENABLE_LOG_RING_BUFFER()
void MaybeSerializeLastLogsForCrashReporting() {}
#endif
}  // namespace perfetto::base

namespace std {
inline namespace _LIBCPP_ABI_NAMESPACE {
[[noreturn]] void __libcpp_verbose_abort(const char*, ...) noexcept { Finish(74); }
}
}  // namespace std

extern "C" char* strerror(int) noexcept { Finish(75); }

extern "C" int ClockSnapshotTestMain() {
  using namespace perfetto::protos::pbzero;
  for (mode = 0; mode < 3; ++mode) {
    const auto snapshot = perfetto::base::CaptureClockSnapshots();
    if (snapshot.size() != (mode == 0 ? 3 : 2))
      return 1;
    if (snapshot[0].clock_id != BUILTIN_CLOCK_BOOTTIME ||
        snapshot[1].clock_id != BUILTIN_CLOCK_MONOTONIC ||
        snapshot[0].timestamp != 12000345000ULL ||
        snapshot[1].timestamp != snapshot[0].timestamp)
      return 2;
#if defined(GTOS_CLOCK_SNAPSHOT_WRONG_EXPECTATION)
    constexpr uint64_t epoch_ns = 946684800009000001ULL;
#else
    constexpr uint64_t epoch_ns = 946684800009000000ULL;
#endif
    if (mode == 0 && (snapshot[2].clock_id != BUILTIN_CLOCK_REALTIME ||
                      snapshot[2].timestamp != epoch_ns))
      return 3;
  }
  return uptime_calls == 3 && realtime_calls == 3 ? 0 : 4;
}

// The fixture runs under the host's IA32 user emulator and injects only the clock.
asm(".globl _start\n_start:\n"
    "andl $-16,%esp\n"
    "call ClockSnapshotTestMain\n"
    "movl %eax,%ebx\nmovl $1,%eax\nint $0x80\nud2\n");
