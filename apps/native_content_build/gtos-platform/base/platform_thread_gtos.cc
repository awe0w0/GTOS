// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/platform_thread.h"

#include <__external_threading>

#include "tools/gtos_native_sdk/native-services.h"

namespace base {

PlatformThreadId PlatformThreadBase::CurrentId() {
  return PlatformThreadId(gtos::native::CurrentProcessInfo().thread_id);
}

PlatformThreadRef PlatformThreadBase::CurrentRef() {
  return PlatformThreadRef(CurrentId().raw());
}

PlatformThreadHandle PlatformThreadBase::CurrentHandle() {
  return PlatformThreadHandle(CurrentId().raw());
}

void PlatformThreadBase::YieldCurrentThread() {
  std::__libcpp_thread_yield();
}

}  // namespace base
