// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_metrics.h"

#include <limits>
#include <common/version.h>
#include <process/cpu_clock_abi.h>
#include <process/system_memory_abi.h>

#include "base/memory/ptr_util.h"
#include "base/memory/page_size.h"
#include "base/numerics/safe_conversions.h"
#include "base/system/sys_info.h"
#include "tools/gtos_native_sdk/native-services.h"

namespace base {

ProcessMetrics::ProcessMetrics(ProcessHandle process) : process_(process) {}

std::unique_ptr<ProcessMetrics> ProcessMetrics::CreateProcessMetrics(
    ProcessHandle process) {
  return WrapUnique(new ProcessMetrics(process));
}

base::expected<TimeDelta, ProcessCPUUsageError>
ProcessMetrics::GetCumulativeCPUUsage() {
  if (process_ != GetCurrentProcessHandle()) {
    return base::unexpected(ProcessCPUUsageError::kProcessNotFound);
  }
  GtosCpuClockReadResult info{};
  const GtosCpuClockReadRequest request{
      GTOS_CPU_CLOCK_ABI_VERSION, GTOS_CPU_CLOCK_PROCESS, 0,
      reinterpret_cast<unsigned>(&info), sizeof(info)};
  unsigned operation = GTOS_SYS_CPU_CLOCK_READ;
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(&request), "c"(sizeof(request)) : "memory", "cc");
  if (operation || info.version != GTOS_CPU_CLOCK_ABI_VERSION ||
      info.scope != GTOS_CPU_CLOCK_PROCESS ||
      info.unit != GTOS_CPU_CLOCK_UNIT_MICROSECONDS ||
      info.source != GTOS_CPU_CLOCK_SOURCE_PIT_RUNNING_SAMPLE ||
      (info.capabilities & GTOS_CPU_CLOCK_REQUIRED_CAPABILITIES) !=
          GTOS_CPU_CLOCK_REQUIRED_CAPABILITIES ||
      info.microseconds > std::numeric_limits<int64_t>::max()) {
    return base::unexpected(ProcessCPUUsageError::kSystemError);
  }
  return Microseconds(static_cast<int64_t>(info.microseconds));
}

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

int SysInfo::NumberOfProcessors() {
  return checked_cast<int>(gtos::native::CurrentProcessInfo().scheduler_cpu_count);
}

int SysInfo::NumberOfEfficientProcessorsImpl() {
  return 0;
}

SysInfo::HardwareInfo SysInfo::GetHardwareInfoSync() {
  return {};
}

ByteSize SysInfo::AmountOfVirtualMemory() {
  const auto info = gtos::native::CurrentProcessInfo();
  return ByteSize(static_cast<uint64_t>(info.maximum_pages) * info.page_bytes);
}

std::optional<SysInfo::DiskSpaceInfo> SysInfo::AmountOfDiskSpace(
    const FilePath&) {
  // The file service reports file sizes but exposes no volume usage query.
  return std::nullopt;
}

std::optional<int64_t> SysInfo::AmountOfFreeDiskSpace(const FilePath& path) {
  return AmountOfDiskSpace(path).transform([](DiskSpaceInfo info) {
    return saturated_cast<int64_t>(info.available.InBytes());
  });
}

std::optional<int64_t> SysInfo::AmountOfTotalDiskSpace(const FilePath& path) {
  return AmountOfDiskSpace(path).transform([](DiskSpaceInfo info) {
    return saturated_cast<int64_t>(info.total.InBytes());
  });
}

std::string SysInfo::OperatingSystemName() {
  return "GTOS";
}

std::string SysInfo::OperatingSystemVersion() {
  return GTOS_VERSION_STRING;
}

void SysInfo::OperatingSystemVersionNumbers(int32_t* major_version,
                                            int32_t* minor_version,
                                            int32_t* bugfix_version) {
  *major_version = GTOS_VERSION_MAJOR;
  *minor_version = GTOS_VERSION_MINOR;
  *bugfix_version = GTOS_VERSION_PATCH;
}

std::string SysInfo::OperatingSystemArchitecture() {
  return "x86";
}

}  // namespace base
