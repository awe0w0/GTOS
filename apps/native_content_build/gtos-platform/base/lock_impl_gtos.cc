// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/synchronization/lock_impl.h"

#include "base/check_op.h"
#include "base/synchronization/lock_metrics_recorder.h"

namespace base::internal {

LockImpl::LockImpl() : native_handle_(_LIBCPP_MUTEX_INITIALIZER) {}

LockImpl::~LockImpl() {
  CHECK_EQ(std::__libcpp_mutex_destroy(&native_handle_), 0);
}

void LockImpl::LockInternal(const LockMetricTagList& tags) {
  LockMetricsRecorder::ScopedLockAcquisitionTimer timer(tags);
  CHECK_EQ(std::__libcpp_mutex_lock(&native_handle_), 0);
}

}  // namespace base::internal

namespace base {

bool KernelSupportsPriorityInheritanceFutex() {
  return false;
}

}  // namespace base
