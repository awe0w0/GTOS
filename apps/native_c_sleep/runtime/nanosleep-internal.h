#ifndef GTOS_NATIVE_NANOSLEEP_INTERNAL_H
#define GTOS_NATIVE_NANOSLEEP_INTERNAL_H
#include <time.h>

namespace gtos_sleep {
inline bool Valid(const timespec& duration) {
  return duration.tv_sec >= 0 && duration.tv_nsec >= 0 &&
         duration.tv_nsec < 1000000000LL;
}
// Inputs are normalized, nonnegative clock samples and a validated duration.
// -1 reports a backwards clock; 0 means still waiting; 1 means elapsed.
inline int Elapsed(const timespec& begin, const timespec& now,
                   const timespec& duration) {
  if (now.tv_sec < begin.tv_sec ||
      (now.tv_sec == begin.tv_sec && now.tv_nsec < begin.tv_nsec)) return -1;
  time_t seconds = now.tv_sec - begin.tv_sec;
  auto nanoseconds = now.tv_nsec - begin.tv_nsec;
  if (nanoseconds < 0) {
    --seconds;
    nanoseconds += 1000000000LL;
  }
  return seconds > duration.tv_sec ||
         (seconds == duration.tv_sec && nanoseconds >= duration.tv_nsec);
}
}  // namespace gtos_sleep
#endif
