// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <process/cpu_clock_abi.h>
#include <time.h>

extern "C" clock_t clock() noexcept {
  static_assert(CLOCKS_PER_SEC == 1000000);
  GtosCpuClockReadResult result{};
  const GtosCpuClockReadRequest request{
      GTOS_CPU_CLOCK_ABI_VERSION, GTOS_CPU_CLOCK_PROCESS, 0,
      reinterpret_cast<unsigned>(&result), sizeof(result)};
  unsigned status = GTOS_SYS_CPU_CLOCK_READ;
  asm volatile("int $0x80"
               : "+a"(status)
               : "b"(&request), "c"(sizeof(request))
               : "memory", "cc");
  if (status != 0 || result.version != GTOS_CPU_CLOCK_ABI_VERSION ||
      result.scope != GTOS_CPU_CLOCK_PROCESS ||
      result.unit != GTOS_CPU_CLOCK_UNIT_MICROSECONDS ||
      result.source != GTOS_CPU_CLOCK_SOURCE_PIT_RUNNING_SAMPLE ||
      (result.capabilities & GTOS_CPU_CLOCK_REQUIRED_CAPABILITIES) !=
          GTOS_CPU_CLOCK_REQUIRED_CAPABILITIES ||
      result.microseconds > LONG_MAX) {
    return static_cast<clock_t>(-1);
  }
  return static_cast<clock_t>(result.microseconds);
}

extern "C" time_t time(time_t* output) noexcept {
  timespec value{};
  if (clock_gettime(CLOCK_REALTIME, &value) != 0)
    return static_cast<time_t>(-1);
  if (output != nullptr)
    *output = value.tv_sec;
  return value.tv_sec;
}
