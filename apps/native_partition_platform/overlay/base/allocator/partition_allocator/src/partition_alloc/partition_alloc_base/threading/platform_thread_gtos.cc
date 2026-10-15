// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "partition_alloc/partition_alloc_base/threading/platform_thread.h"

#include <time.h>

#include "tools/gtos_native_sdk/native-services.h"

#if !PA_BUILDFLAG(IS_GTOS) || !defined(__i386__)
#error This provider requires the actual GTOS native IA32 platform
#endif

namespace partition_alloc::internal::base {

// Query the kernel directly: allocator thread identification cannot allocate
// memory or initialize compiler TLS while an allocator lock is held.
PlatformThreadId PlatformThread::CurrentId() {
  return gtos::native::CurrentProcessInfo().thread_id;
}

PlatformThreadRef PlatformThread::CurrentRef() {
  return PlatformThreadRef(CurrentId());
}

PlatformThreadHandle PlatformThread::CurrentHandle() {
  return PlatformThreadHandle(CurrentId());
}

void PlatformThread::Sleep(TimeDelta duration) {
  const int64_t microseconds = duration.InMicroseconds();
  if (microseconds <= 0) {
    return;
  }
  timespec requested = {};
  requested.tv_sec = microseconds / 1000000;
  requested.tv_nsec = (microseconds % 1000000) * 1000;
  // ABI1 has no user signals or EINTR remainder. The existing native provider
  // uses the monotonic clock and yields; a contract failure cannot be ignored.
  if (nanosleep(&requested, nullptr) != 0) {
    gtos::native::UnsupportedService();
  }
}

}  // namespace partition_alloc::internal::base
