#ifndef GTOS_NATIVE_CLOCK_GETTIME_INTERNAL_H
#define GTOS_NATIVE_CLOCK_GETTIME_INTERNAL_H
#include <errno.h>
#include <time.h>
#include "src/base/platform/gtos-native-api/clock_abi.h"
#include "src/base/platform/gtos-native-api/realtime_abi.h"

namespace gtos_clock {
inline int Error(int status) {
  switch (status) {
    case GTOS_CLOCK_ERR_BAD_SIZE: return EINVAL;
    case GTOS_CLOCK_ERR_BAD_ADDRESS: return EFAULT;
    case GTOS_CLOCK_ERR_UNSUPPORTED: return ENOSYS;
    case GTOS_CLOCK_ERR_OVERFLOW: return EOVERFLOW;
    default: return EIO;
  }
}
inline timespec Convert(unsigned long long microseconds) {
  // Full unsigned microsecond domain fits the actual signed 64-bit seconds.
  return {static_cast<time_t>(microseconds / 1000000ULL),
          static_cast<decltype(timespec::tv_nsec)>(
              (microseconds % 1000000ULL) * 1000ULL)};
}
inline int Decode(const GtosClockReadResult& value, timespec& result) {
  if (value.version != GTOS_CLOCK_ABI_VERSION ||
      value.clock_id != GTOS_CLOCK_ID_MONOTONIC ||
      value.unit != GTOS_CLOCK_UNIT_MICROSECONDS ||
      value.source != GTOS_CLOCK_SOURCE_PIT_DELIVERED_IRQ ||
      value.capabilities != GTOS_CLOCK_REQUIRED_CAPABILITIES ||
      value.resolution_us != GTOS_CLOCK_RESOLUTION_US ||
      value.pit_input_hz != GTOS_CLOCK_PIT_INPUT_HZ ||
      value.pit_divisor != GTOS_CLOCK_PIT_DIVISOR) return EIO;
  result = Convert(value.microseconds);
  return 0;
}
inline int Decode(const GtosRealtimeReadResult& value, timespec& result) {
  if (value.version != GTOS_REALTIME_ABI_VERSION ||
      value.unit != GTOS_REALTIME_UNIT_MICROSECONDS ||
      value.source != GTOS_REALTIME_SOURCE_CMOS_PIT ||
      value.capabilities != GTOS_REALTIME_REQUIRED_CAPABILITIES ||
      value.resolution_us != GTOS_REALTIME_RESOLUTION_US ||
      value.anchor_uncertainty_us != GTOS_REALTIME_ANCHOR_UNCERTAINTY_US) return EIO;
  result = Convert(value.microseconds);
  return 0;
}
}  // namespace gtos_clock
#endif
