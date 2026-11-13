// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/synchronization/condition_variable.h"

#include <errno.h>
#include <gtos_condition_variable.h>
#include <time.h>

#include <algorithm>
#include <limits>
#include <optional>

#include "base/check_op.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"

namespace base {

ConditionVariable::ConditionVariable(Lock* user_lock)
    : user_mutex_(user_lock->lock_.native_handle())
#if DCHECK_IS_ON()
      ,
      user_lock_(user_lock)
#endif
{}

ConditionVariable::~ConditionVariable() {
  CHECK_EQ(std::__libcpp_condvar_destroy(&condition_), 0);
}

void ConditionVariable::Wait(const Location& location) {
  WaitImpl(location, nullptr);
}

void ConditionVariable::TimedWait(TimeDelta max_time, const Location& location) {
  if (max_time.is_max()) {
    Wait(location);
    return;
  }

  timespec deadline{};
  CHECK_EQ(clock_gettime(CLOCK_MONOTONIC, &deadline), 0);
  const int64_t micros = std::max(max_time, TimeDelta()).InMicroseconds();
  const int64_t nanos = deadline.tv_nsec +
      (micros % Time::kMicrosecondsPerSecond) * Time::kNanosecondsPerMicrosecond;
  const int64_t seconds = micros / Time::kMicrosecondsPerSecond +
      nanos / Time::kNanosecondsPerSecond;
  if (deadline.tv_sec > std::numeric_limits<time_t>::max() - seconds) {
    deadline.tv_sec = std::numeric_limits<time_t>::max();
    deadline.tv_nsec = Time::kNanosecondsPerSecond - 1;
  } else {
    deadline.tv_sec = static_cast<time_t>(deadline.tv_sec + seconds);
    deadline.tv_nsec = static_cast<decltype(deadline.tv_nsec)>(
        nanos % Time::kNanosecondsPerSecond);
  }
  WaitImpl(location, &deadline);
}

void ConditionVariable::WaitImpl(const Location& location,
                                 const timespec* deadline) {
  std::optional<internal::ScopedBlockingCallWithBaseSyncPrimitives> blocking;
  if (waiting_is_blocking_) {
    blocking.emplace(location, BlockingType::MAY_BLOCK);
  }
#if DCHECK_IS_ON()
  user_lock_->CheckHeldAndUnmark();
#endif
  const int result =
      deadline ? std::__libcpp_condvar_timedwait_monotonic(
                     &condition_, user_mutex_, deadline)
               : std::__libcpp_condvar_wait(&condition_, user_mutex_);
  CHECK(result == 0 || (deadline && result == ETIMEDOUT)) << result;
#if DCHECK_IS_ON()
  user_lock_->CheckUnheldAndMark();
#endif
}

void ConditionVariable::Broadcast() {
  CHECK_EQ(std::__libcpp_condvar_broadcast(&condition_), 0);
}

void ConditionVariable::Signal() {
  CHECK_EQ(std::__libcpp_condvar_signal(&condition_), 0);
}

}  // namespace base
