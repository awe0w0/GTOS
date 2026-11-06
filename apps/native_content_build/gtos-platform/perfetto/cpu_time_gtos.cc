// Self-only, delivered-PIT CPU accounting; distinct from uptime and UTC.
#include "perfetto/base/time.h"

#include <stdint.h>

#include <process/clock_abi.h>
#include <process/cpu_clock_abi.h>

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_GTOS) || !defined(__i386__)
#error Native CPU time requires the GTOS IA32 CPU clock ABI
#endif

#if defined(GTOS_PERFETTO_CPU_TIME_TEST)
extern "C" int GtosPerfettoCpuTimeTestCall(unsigned, const void*, unsigned);
#endif

namespace perfetto {
namespace base {
namespace {
TimeNanos ReadCpuTime(unsigned scope) {
  GtosCpuClockReadResult value{};
  const GtosCpuClockReadRequest request{
      GTOS_CPU_CLOCK_ABI_VERSION, scope, 0,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(&value)), sizeof(value)};
#if defined(GTOS_PERFETTO_CPU_TIME_TEST)
  const int status = GtosPerfettoCpuTimeTestCall(
      GTOS_SYS_CPU_CLOCK_READ, &request, sizeof(request));
#else
  unsigned operation = GTOS_SYS_CPU_CLOCK_READ;
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(reinterpret_cast<uintptr_t>(&request)), "c"(sizeof(request))
               : "memory", "cc");
  const int status = static_cast<int>(operation);
#endif
  PERFETTO_CHECK(status == 0);
  PERFETTO_CHECK(value.version == GTOS_CPU_CLOCK_ABI_VERSION &&
                 value.scope == scope &&
                 value.unit == GTOS_CPU_CLOCK_UNIT_MICROSECONDS &&
                 value.source == GTOS_CPU_CLOCK_SOURCE_PIT_RUNNING_SAMPLE &&
                 (value.capabilities & GTOS_CPU_CLOCK_REQUIRED_CAPABILITIES) ==
                     GTOS_CPU_CLOCK_REQUIRED_CAPABILITIES &&
                 value.resolution_us == GTOS_CLOCK_RESOLUTION_US &&
                 value.pit_input_hz == GTOS_CLOCK_PIT_INPUT_HZ &&
                 value.pit_divisor == GTOS_CLOCK_PIT_DIVISOR);
  PERFETTO_CHECK(value.microseconds <= static_cast<uint64_t>(INT64_MAX) / 1000);
  return TimeNanos(static_cast<int64_t>(value.microseconds * 1000));
}
}

TimeNanos GetThreadCPUTimeNs() { return ReadCpuTime(GTOS_CPU_CLOCK_THREAD); }
TimeNanos GetProcessCPUTimeNs() { return ReadCpuTime(GTOS_CPU_CLOCK_PROCESS); }

}  // namespace base
}  // namespace perfetto
