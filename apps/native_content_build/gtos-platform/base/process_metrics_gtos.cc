// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_metrics.h"

#include <process/system_memory_abi.h>

#include "base/memory/page_size.h"
#include "base/system/sys_info.h"

namespace base {

bool GetSystemMemoryInfo(SystemMemoryInfo* output) {
  GtosSystemMemoryInfo info{};
  const GtosSystemMemoryRequest request{GTOS_SYSTEM_MEMORY_ABI_VERSION,
                                         reinterpret_cast<unsigned>(&info)};
  unsigned operation = GTOS_SYS_SYSTEM_MEMORY_INFO;
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(&request), "c"(sizeof(request)) : "memory", "cc");
  if (operation || info.version != GTOS_SYSTEM_MEMORY_ABI_VERSION ||
      info.page_bytes != GetPageSize() || !info.total_frames ||
      info.free_frames > info.total_frames) {
    return false;
  }

  SystemMemoryInfo sample;
  sample.total = ByteSize(static_cast<uint64_t>(info.total_frames) * info.page_bytes);
  sample.free = ByteSize(static_cast<uint64_t>(info.free_frames) * info.page_bytes);
  *output = sample;
  return true;
}

ByteSize SystemMemoryInfo::GetAvailablePhysicalMemory() const {
  return free;
}

size_t GetSystemCommitCharge() {
  SystemMemoryInfo info;
  if (!GetSystemMemoryInfo(&info)) {
    return 0;
  }
  return static_cast<size_t>(ByteSize::FromByteSizeDelta(info.total - info.free).InKiB());
}

ByteSize SysInfo::AmountOfTotalPhysicalMemoryImpl() {
  SystemMemoryInfo info;
  return GetSystemMemoryInfo(&info) ? info.total : ByteSize();
}

ByteSize SysInfo::AmountOfAvailablePhysicalMemoryImpl() {
  SystemMemoryInfo info;
  return GetSystemMemoryInfo(&info) ? info.free : ByteSize();
}

}  // namespace base
