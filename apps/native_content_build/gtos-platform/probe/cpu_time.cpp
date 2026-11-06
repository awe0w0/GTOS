#include "cpu_time_record.h"
#include "perfetto/base/time.h"
#include <process/clock_abi.h>
#include <process/cpu_clock_abi.h>

extern "C" volatile gtos_perfetto_cpu_probe::Record native_cpu_record
    __attribute__((section(".data.vm_record"), used, aligned(4))) = {
        1, GTOS_CPU_GUEST_MODE, 0, 0, 0, 0, 0, 0, 0, 0, 0};

namespace {
void Write(const char* text) {
  unsigned size = 0;
  while (text[size]) ++size;
  unsigned operation = 0x4701;
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(text), "c"(size) : "memory", "cc");
}
int Call(unsigned operation, const void* pointer, unsigned bytes) {
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(pointer), "c"(bytes) : "memory", "cc");
  return static_cast<int>(operation);
}
void Check(bool value) {
  native_cpu_record.checks = native_cpu_record.checks + 1;
  if (!value) native_cpu_record.failures = native_cpu_record.failures + 1;
}
unsigned long long RawCpu(unsigned scope) {
  GtosCpuClockReadResult value{};
  const GtosCpuClockReadRequest request{1, scope, 0, reinterpret_cast<unsigned>(&value), sizeof(value)};
  native_cpu_record.cpu_queries = native_cpu_record.cpu_queries + 1;
  if (Call(GTOS_SYS_CPU_CLOCK_READ, &request, sizeof(request)) != 0 ||
      value.version != 1 || value.scope != scope || value.unit != 1 || value.source != 1 ||
      value.capabilities != 7 || value.resolution_us != GTOS_CLOCK_RESOLUTION_US ||
      value.pit_input_hz != GTOS_CLOCK_PIT_INPUT_HZ || value.pit_divisor != GTOS_CLOCK_PIT_DIVISOR)
    native_cpu_record.failures = native_cpu_record.failures + 1;
  return value.microseconds;
}
unsigned long long Wall() {
  GtosClockReadResult value{};
  const GtosClockReadRequest request{1, GTOS_CLOCK_ID_MONOTONIC, 0,
                                    reinterpret_cast<unsigned>(&value), sizeof(value)};
  native_cpu_record.wall_queries = native_cpu_record.wall_queries + 1;
  if (Call(GTOS_SYS_CLOCK_READ, &request, sizeof(request)) != 0 ||
      value.version != 1 || value.clock_id != 1 || value.unit != 1 || value.source != 1 ||
      value.capabilities != 7 || value.resolution_us != GTOS_CLOCK_RESOLUTION_US ||
      value.pit_input_hz != GTOS_CLOCK_PIT_INPUT_HZ || value.pit_divisor != GTOS_CLOCK_PIT_DIVISOR)
    native_cpu_record.failures = native_cpu_record.failures + 1;
  return value.microseconds;
}
long long Thread() {
  native_cpu_record.cpu_queries = native_cpu_record.cpu_queries + 1;
  return perfetto::base::GetThreadCPUTimeNs().count();
}
long long Process() {
  native_cpu_record.cpu_queries = native_cpu_record.cpu_queries + 1;
  return perfetto::base::GetProcessCPUTimeNs().count();
}
}

extern "C" char* strerror(int) noexcept { return const_cast<char*>("native CPU clock"); }
namespace perfetto {
namespace base {
void LogMessage(LogLev, const char*, int, const char*, ...) { Write("PCPU ASSERT\n"); }
#if PERFETTO_ENABLE_LOG_RING_BUFFER()
void MaybeSerializeLastLogsForCrashReporting() {}
#endif
}
}

extern "C" int NativeEntry() {
  Write("PCPU CONSUMER BEGIN\n");
  const auto wall_start = Wall();
  const auto raw_before = RawCpu(GTOS_CPU_CLOCK_THREAD);
  const auto start = Thread();
  const auto raw_after = RawCpu(GTOS_CPU_CLOCK_THREAD);
  Check(start >= 0 && raw_before * 1000 <= static_cast<unsigned long long>(start));
  Check(static_cast<unsigned long long>(start) <= raw_after * 1000);
  Check(start % 1000 == 0);
  const long long required = (3 + GTOS_CPU_GUEST_MODE) * 10000000LL;
  long long current = start;
  while (current - start < required) {
    for (volatile unsigned index = 0; index < 50000; ++index) {}
    current = Thread();
    if (Wall() - wall_start > 2000000) {
      Write("PCPU CONSUMER TIMEOUT\n");
      return 9;
    }
  }
  const auto process = Process();
  const auto raw_process = RawCpu(GTOS_CPU_CLOCK_PROCESS);
  const auto wall_end = Wall();
  Check(current - start >= required);
  Check(current >= start);
  Check(process >= current);
  Check(process >= 0 && static_cast<unsigned long long>(process) <= raw_process * 1000);
  Check(current % 1000 == 0);
  Check(process % 1000 == 0);
  Check(wall_end >= wall_start);
  Check((wall_end - wall_start) * 1000 >= static_cast<unsigned long long>(current - start));
  Check(wall_end - wall_start <= 2000000);
  native_cpu_record.start_us = static_cast<unsigned>(start / 1000);
  native_cpu_record.end_us = static_cast<unsigned>(current / 1000);
  native_cpu_record.process_us = static_cast<unsigned>(process / 1000);
  native_cpu_record.wall_us = static_cast<unsigned>(wall_end - wall_start);
#if defined(GTOS_CPU_GUEST_WRONG_EXPECTATION)
  Check(false);
#endif
  native_cpu_record.stage = 2;
  Write(native_cpu_record.failures ? "PCPU CONSUMER FAIL\n" : "PCPU CONSUMER PASS\n");
  return native_cpu_record.failures ? 1 : 0;
}
