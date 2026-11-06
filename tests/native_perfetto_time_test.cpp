#include "perfetto/base/time.h"
#include <errno.h>
#include <limits.h>
#include <process/clock_abi.h>
#include <process/cpu_clock_abi.h>

namespace {
unsigned calls = 0;
unsigned failed = 0;
unsigned checks = 0;
time_t seconds = 12;
long long nanos = 345000;
int clock_status = 0;
unsigned cpu_calls = 0;
unsigned thread_calls = 0;
unsigned process_calls = 0;
unsigned long long thread_us = 0;
unsigned long long process_us = 0;
void Write(const char* text) {
  unsigned bytes = 0;
  while (text[bytes]) ++bytes;
  unsigned eax = 4;
  asm volatile("int $0x80" : "+a"(eax) : "b"(1), "c"(text), "d"(bytes) : "memory", "cc");
}
void Check(bool value) {
  ++checks;
  if (!value) { ++failed; Write("CHECK_FAILED\n"); }
}
}

extern "C" {
int GtosPerfettoCpuTimeTestCall(unsigned operation, const void* pointer, unsigned bytes) {
  ++cpu_calls;
  const auto& request = *static_cast<const GtosCpuClockReadRequest*>(pointer);
  Check(operation == GTOS_SYS_CPU_CLOCK_READ && bytes == sizeof(request));
  Check(request.version == GTOS_CPU_CLOCK_ABI_VERSION && request.flags == 0 &&
        request.result_bytes == sizeof(GtosCpuClockReadResult) && request.result_va != 0);
  Check(request.scope == GTOS_CPU_CLOCK_THREAD || request.scope == GTOS_CPU_CLOCK_PROCESS);
  if (request.scope == GTOS_CPU_CLOCK_THREAD) ++thread_calls; else ++process_calls;
  GtosCpuClockReadResult result{GTOS_CPU_CLOCK_ABI_VERSION, request.scope,
      GTOS_CPU_CLOCK_UNIT_MICROSECONDS, GTOS_CPU_CLOCK_SOURCE_PIT_RUNNING_SAMPLE,
      GTOS_CPU_CLOCK_REQUIRED_CAPABILITIES, GTOS_CLOCK_RESOLUTION_US,
      request.scope == GTOS_CPU_CLOCK_THREAD ? thread_us : process_us, 3,
      GTOS_CLOCK_PIT_INPUT_HZ, GTOS_CLOCK_PIT_DIVISOR};
#if GTOS_TIME_CASE == 7
  return GTOS_CPU_CLOCK_ERR_UNSUPPORTED;
#elif GTOS_TIME_CASE == 8
  ++result.version;
#elif GTOS_TIME_CASE == 9
  result.scope = 3;
#elif GTOS_TIME_CASE == 10
  ++result.unit;
#elif GTOS_TIME_CASE == 11
  ++result.source;
#elif GTOS_TIME_CASE == 12
  result.capabilities &= ~GTOS_CPU_CLOCK_CAP_SELF_ONLY;
#elif GTOS_TIME_CASE == 13
  ++result.resolution_us;
#elif GTOS_TIME_CASE == 14
  ++result.pit_input_hz;
#elif GTOS_TIME_CASE == 15
  ++result.pit_divisor;
#elif GTOS_TIME_CASE == 16
  result.microseconds = 9223372036854776ULL;
#endif
  *reinterpret_cast<GtosCpuClockReadResult*>(request.result_va) = result;
  return 0;
}
int* __llvm_libc_errno() noexcept { static int value; return &value; }
char* strerror(int) noexcept { return const_cast<char*>("test errno"); }
// This replaces only the C clock boundary, not Perfetto's checked conversion.
int clock_gettime(clockid_t id, timespec* value) noexcept {
  ++calls;
  if (id != CLOCK_MONOTONIC) { Write("WRONG_CLOCK\n"); return -1; }
  if (clock_status) return clock_status;
  *value = {seconds, nanos};
  return 0;
}
void* memcpy(void* to, const void* from, unsigned size) {
  for (unsigned i = 0; i < size; ++i) static_cast<char*>(to)[i] = static_cast<const char*>(from)[i];
  return to;
}
void* memset(void* to, int byte, unsigned size) {
  for (unsigned i = 0; i < size; ++i) static_cast<char*>(to)[i] = static_cast<char>(byte);
  return to;
}
void __assert_fail(const char*, const char*, unsigned, const char*) {
  Write("CALENDAR_ASSERT_UNEXPECTED\n"); __builtin_trap();
}
}

namespace perfetto {
namespace base {
void LogMessage(LogLev, const char*, int, const char*, ...) { Write("INVALID_CLOCK\n"); }
#if PERFETTO_ENABLE_LOG_RING_BUFFER()
void MaybeSerializeLastLogsForCrashReporting() {}
#endif
}
}

extern "C" int TimeTestMain() {
  using namespace perfetto::base;
  static_assert(sizeof(time_t) == 8 && sizeof(timespec) == 16, "native clock layout");
#if GTOS_TIME_CASE != 0
  Write("INVALID_REQUEST\n");
#if GTOS_TIME_CASE == 1
  clock_status = -1;
#elif GTOS_TIME_CASE == 2
  seconds = -1;
#elif GTOS_TIME_CASE == 3
  nanos = -1;
#elif GTOS_TIME_CASE == 4
  nanos = 1000000000;
#elif GTOS_TIME_CASE == 5
  seconds = 9223372037LL;
  nanos = 0;
#elif GTOS_TIME_CASE == 6
  seconds = 9223372036LL;
  nanos = 854775808;
#endif
#if GTOS_TIME_CASE < 7
  (void)GetWallTimeNs();
#elif GTOS_TIME_CASE % 2
  (void)GetThreadCPUTimeNs();
#else
  (void)GetProcessCPUTimeNs();
#endif
  Write("RETURNED_UNEXPECTED\n");
  return 8;
#else
  Check(GetWallTimeNs().count() == 12000345000LL);
  Check(GetWallTimeRawNs().count() == 12000345000LL);
  Check(GetBootTimeNs().count() == 12000345000LL);
  Check(calls == 3);
  Check(GetThreadCPUTimeNs().count() == 0);
  Check(GetProcessCPUTimeNs().count() == 0);
  Check(calls == 3 && cpu_calls == 2 && thread_calls == 1 && process_calls == 1);
  thread_us = 30000; process_us = 70000;
  Check(GetThreadCPUTimeNs().count() == 30000000);
  Check(GetProcessCPUTimeNs().count() == 70000000);
  thread_us = 40000;
  Check(GetThreadCPUTimeNs().count() == 40000000);
  Check(calls == 3 && cpu_calls == 5 && thread_calls == 3 && process_calls == 2);
  process_us = 9223372036854775ULL;
  Check(GetProcessCPUTimeNs().count() == 9223372036854775000LL);
  seconds = 0; nanos = 0;
  Check(GetWallTimeNs().count() == 0);
  seconds = 9223372036LL; nanos = 854775807;
  Check(GetWallTimeNs().count() == 9223372036854775807LL);
  seconds = 100; nanos = 999999999;
  Check(GetBootTimeS().count() == 100);
  Check(GetBootTimeMs().count() == 100999);
  Check(GetWallTimeS().count() == 100);
  Check(GetWallTimeMs().count() == 100999);

  struct CalendarCase { int year, month, day; long long epoch; };
  const CalendarCase dates[] = {
      {1900, 1, 1, -2208988800LL}, {1969, 12, 31, -86400LL},
      {1970, 1, 1, 0}, {1972, 2, 29, 68169600LL},
      {2000, 1, 1, 946684800LL}, {2000, 2, 29, 951782400LL},
      {2038, 1, 19, 2147472000LL}, {2100, 1, 1, 4102444800LL}};
  for (const auto& value : dates) {
    tm input{};
    input.tm_year = value.year - 1900;
    input.tm_mon = value.month - 1;
    input.tm_mday = value.day;
    errno = 57;
    Check(TimeGm(&input) == value.epoch);
    Check(errno == 57);
    Check(input.tm_year == value.year - 1900 && input.tm_mon == value.month - 1 && input.tm_mday == value.day);
    Check(MkTime(value.year, value.month, value.day, 0, 0, 0) == value.epoch);
  }
  tm normalized{};
  normalized.tm_year = 70; normalized.tm_mon = 12; normalized.tm_mday = 1;
  Check(TimeGm(&normalized) == 31536000);
  Check(normalized.tm_year == 71 && normalized.tm_mon == 0 && normalized.tm_mday == 1);
  normalized = {}; normalized.tm_year = 70; normalized.tm_mday = 1; normalized.tm_sec = -1;
  Check(TimeGm(&normalized) == -1 && normalized.tm_year == 69 && normalized.tm_mon == 11 && normalized.tm_mday == 31);
  Check(normalized.tm_hour == 23 && normalized.tm_min == 59 && normalized.tm_sec == 59);
#if defined(GTOS_TIME_WRONG_EXPECTATION)
  Check(GetWallTimeNs().count() == 1);
#endif
  Write(failed ? "TIME_CHECK_FAIL\n" : "TIME_CHECK_PASS\n");
  return failed ? 1 : 0;
#endif
}
