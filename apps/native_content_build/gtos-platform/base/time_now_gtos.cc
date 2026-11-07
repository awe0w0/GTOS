// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <time.h>

#include <optional>

#include "base/check.h"
#include "base/notreached.h"
#include "base/time/time.h"
#include "base/time/time_override.h"

namespace base {
namespace {

std::optional<TimeDelta> MaybeReadClock(clockid_t clock) {
  timespec value{};
  if (clock_gettime(clock, &value) != 0) {
    return std::nullopt;
  }
  return Seconds(value.tv_sec) + Nanoseconds(value.tv_nsec);
}

TimeDelta ReadClock(clockid_t clock) {
  const auto value = MaybeReadClock(clock);
  CHECK(value.has_value());
  return *value;
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

std::optional<TimeTicks> MaybeTimeTicksNowIgnoringOverride() {
  const auto elapsed = MaybeReadClock(CLOCK_MONOTONIC);
  if (!elapsed) {
    return std::nullopt;
  }
  return TimeTicks() + *elapsed;
}

TimeTicks TimeTicksLowResolutionNowIgnoringOverride() {
  return TimeTicksNowIgnoringOverride();
}

time_internal::RealTicks RealTicksNowIgnoringOverride() {
  // GTOS has no system suspend clock separate from native monotonic uptime.
  return time_internal::RealTicks() + ReadClock(CLOCK_MONOTONIC);
}

ThreadTicks ThreadTicksNowIgnoringOverride() {
  NOTREACHED();
}

}  // namespace subtle

TimeTicks::Clock TimeTicks::GetClock() {
  return Clock::GTOS_CLOCK_MONOTONIC;
}

bool TimeTicks::IsHighResolution() {
  return false;  // Native uptime is sampled at delivered BSP PIT ticks.
}

bool TimeTicks::IsConsistentAcrossProcesses() {
  return true;
}

}  // namespace base
