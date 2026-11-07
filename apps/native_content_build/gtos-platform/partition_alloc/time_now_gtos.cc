// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctime>

#include "partition_alloc/partition_alloc_base/check.h"
#include "partition_alloc/partition_alloc_base/notreached.h"
#include "partition_alloc/partition_alloc_base/time/time.h"

#if !PA_BUILDFLAG(IS_GTOS) || !defined(__i386__)
#error "This provider requires the GTOS IA32 clock ABI."
#endif

namespace partition_alloc::internal::base {
namespace {

TimeDelta ReadClock(clockid_t clock) {
  timespec value{};
  PA_BASE_CHECK(clock_gettime(clock, &value) == 0);
  return Seconds(value.tv_sec) + Nanoseconds(value.tv_nsec);
}

}  // namespace

namespace subtle {

Time TimeNowIgnoringOverride() {
  return Time::UnixEpoch() + ReadClock(CLOCK_REALTIME);
}

Time TimeNowFromSystemTimeIgnoringOverride() {
  return TimeNowIgnoringOverride();
}

TimeTicks TimeTicksNowIgnoringOverride() {
  return TimeTicks() + ReadClock(CLOCK_MONOTONIC);
}

ThreadTicks ThreadTicksNowIgnoringOverride() {
  PA_NOTREACHED();
}

}  // namespace subtle

TimeTicks::Clock TimeTicks::GetClock() {
  return Clock::GTOS_CLOCK_MONOTONIC;
}

bool TimeTicks::IsHighResolution() {
  return false;  // The native monotonic source is delivered BSP PIT ticks.
}

bool TimeTicks::IsConsistentAcrossProcesses() {
  return true;
}

}  // namespace partition_alloc::internal::base
