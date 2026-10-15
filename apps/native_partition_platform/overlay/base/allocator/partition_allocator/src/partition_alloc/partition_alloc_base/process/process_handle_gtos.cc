// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "partition_alloc/partition_alloc_base/process/process_handle.h"

#include "tools/gtos_native_sdk/native-services.h"

#if !PA_BUILDFLAG(IS_GTOS) || !defined(__i386__)
#error This provider requires the actual GTOS native IA32 platform
#endif

namespace partition_alloc::internal::base {

ProcessId GetCurrentProcId() {
  return gtos::native::CurrentProcessInfo().process_id;
}

}  // namespace partition_alloc::internal::base
