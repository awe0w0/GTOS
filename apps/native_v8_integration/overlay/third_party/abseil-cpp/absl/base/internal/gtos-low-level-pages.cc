// Copyright 2026 the GTOS project authors.
// Native data-page backing without malloc, POSIX mapping or signal services.
#include "gtos-low-level-pages.h"

#include <stdint.h>

#include <process/abi.h>
#include <process/info_abi.h>
#include <process/vm_abi.h>

#if !defined(__GTOS__) || !defined(__i386__) || \
    !defined(GTOS_ISOLATE_COMPILE_DIAGNOSTIC) || defined(__linux__) || \
    defined(__unix__) || defined(_WIN32)
#error This backend requires the private single-user-task GTOS IA32 diagnostic
#endif

static_assert(sizeof(void*) == 4 && sizeof(size_t) == 4 && sizeof(unsigned) == 4,
              "GTOS low-level page ABI requires IA32");

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {
namespace {
constexpr unsigned kMaximumRegions = 32;
constexpr unsigned kRegionBegin = 0x80000000U;
constexpr unsigned kRegionEnd = 0xBFFFC000U;
constexpr unsigned kMaximumHandle = 0x7FFFFFFFU;

struct Region {
  unsigned handle, base, length;
};
Region regions[kMaximumRegions];
unsigned owner_process;
bool operation_active;

[[noreturn]] void ContractFailure() { __builtin_trap(); }

class PageOperation {
 public:
  PageOperation() {
    if (operation_active) ContractFailure();
    operation_active = true;
  }
  ~PageOperation() { operation_active = false; }

  PageOperation(const PageOperation&) = delete;
  PageOperation& operator=(const PageOperation&) = delete;
};

int NativeCall(unsigned call, const void* request, unsigned bytes) {
  asm volatile("int $0x80"
               : "+a"(call)
               : "b"(static_cast<unsigned>(reinterpret_cast<uintptr_t>(request))),
                 "c"(bytes)
               : "memory", "cc");
  return static_cast<int>(call);
}

GtosProcessInfoResult CurrentProcessInfo() {
  GtosProcessInfoResult result = {};
  GtosProcessInfoRequest request = {
      GTOS_PROCESS_INFO_ABI_VERSION, 0,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(&result)),
      sizeof(result)};
  if (NativeCall(GTOS_SYS_PROCESS_INFO, &request, sizeof(request)) != 0 ||
      result.version != GTOS_PROCESS_INFO_ABI_VERSION ||
      result.process_id == 0 || result.process_id > kMaximumHandle ||
      result.thread_id != result.process_id ||
      result.stack_begin >= result.stack_end ||
      result.user_begin > result.stack_begin ||
      result.stack_end > result.user_end || result.user_begin >= result.user_end ||
      result.page_bytes != GTOS_VM_PAGE_BYTES || result.maximum_pages == 0 ||
      result.maximum_regions == 0 || result.maximum_regions > kMaximumRegions ||
      result.maximum_parallel_threads != 1 || result.scheduler_cpu_count != 1) {
    ContractFailure();
  }
  if (owner_process != 0 && owner_process != result.process_id) {
    ContractFailure();
  }
  owner_process = result.process_id;
  return result;
}

bool ValidRegion(const Region& region, const GtosProcessInfoResult& info) {
  return region.handle != 0 && region.handle <= kMaximumHandle &&
         region.base >= kRegionBegin && region.base < kRegionEnd &&
         region.length != 0 &&
         ((region.base | region.length) & (GTOS_VM_PAGE_BYTES - 1)) == 0 &&
         region.length <= kRegionEnd - region.base &&
         region.base >= info.user_begin && region.base < info.user_end &&
         region.length <= info.user_end - region.base;
}

bool Overlap(const Region& first, const Region& second) {
  if (first.base <= second.base) {
    return first.length > second.base - first.base;
  }
  return second.length > first.base - second.base;
}

void Validate(const GtosProcessInfoResult& info) {
  for (unsigned i = 0; i < kMaximumRegions; ++i) {
    const Region& region = regions[i];
    if (region.handle == 0) {
      if (region.base != 0 || region.length != 0) ContractFailure();
      continue;
    }
    if (i >= info.maximum_regions || !ValidRegion(region, info)) {
      ContractFailure();
    }
    for (unsigned j = 0; j < i; ++j) {
      if (regions[j].handle != 0 &&
          (regions[j].handle == region.handle || Overlap(regions[j], region))) {
        ContractFailure();
      }
    }
  }
}

bool CurrentRegion(const Region& region) {
  GtosVmRegionInfo result = {};
  GtosVmQueryRequest request = {
      GTOS_VM_ABI_VERSION, region.handle,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(&result))};
  const int status = NativeCall(GTOS_SYS_VM_QUERY, &request, sizeof(request));
  if (status > 0) ContractFailure();
  if (status < 0) return false;
  if (result.version != GTOS_VM_ABI_VERSION || result.handle != region.handle ||
      result.base != region.base || result.length != region.length ||
      result.resident_pages != region.length / GTOS_VM_PAGE_BYTES) {
    ContractFailure();
  }
  return true;
}

bool ReleaseRegion(Region* region) {
  GtosVmControlRequest request = {GTOS_VM_ABI_VERSION, region->handle, 0};
  const int status = NativeCall(GTOS_SYS_VM_RELEASE, &request, sizeof(request));
  if (status > 0) ContractFailure();
  if (status < 0) return false;
  *region = {};
  return true;
}
}  // namespace

size_t GtosLowLevelPageSize() {
  PageOperation operation;
  const GtosProcessInfoResult info = CurrentProcessInfo();
  Validate(info);
  return info.page_bytes;
}

void* GtosLowLevelMapPages(size_t size, size_t alignment) {
  PageOperation operation;
  const GtosProcessInfoResult info = CurrentProcessInfo();
  Validate(info);
  if (size == 0 || (size & (GTOS_VM_PAGE_BYTES - 1)) != 0 ||
      size > kRegionEnd - kRegionBegin ||
      size / GTOS_VM_PAGE_BYTES > info.maximum_pages ||
      alignment < GTOS_VM_PAGE_BYTES || (alignment & (alignment - 1)) != 0) {
    return nullptr;
  }
  Region* slot = nullptr;
  for (unsigned i = 0; i < info.maximum_regions; ++i) {
    if (regions[i].handle == 0) {
      slot = &regions[i];
      break;
    }
  }
  if (slot == nullptr) return nullptr;

  GtosVmReserveResult result = {};
  GtosVmReserveRequest reserve = {
      GTOS_VM_ABI_VERSION, static_cast<unsigned>(size),
      static_cast<unsigned>(alignment), 0,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(&result))};
  int status = NativeCall(GTOS_SYS_VM_RESERVE, &reserve, sizeof(reserve));
  if (status > 0) ContractFailure();
  if (status < 0) return nullptr;
  const Region candidate = {result.handle, result.base, result.length};
  if (result.version != GTOS_VM_ABI_VERSION ||
      result.page_size != GTOS_VM_PAGE_BYTES || result.length != size ||
      !ValidRegion(candidate, info) || (result.base & (alignment - 1)) != 0) {
    ContractFailure();
  }
  for (const Region& region : regions) {
    if (region.handle != 0 &&
        (region.handle == candidate.handle || Overlap(region, candidate))) {
      ContractFailure();
    }
  }
  *slot = candidate;

  GtosVmRangeRequest commit = {
      GTOS_VM_ABI_VERSION, result.handle, 0, static_cast<unsigned>(size),
      GTOS_VM_READ_WRITE};
  status = NativeCall(GTOS_SYS_VM_SET_PERMISSIONS, &commit, sizeof(commit));
  if (status > 0) ContractFailure();
  if (status < 0 || !CurrentRegion(*slot)) {
    if (!ReleaseRegion(slot)) ContractFailure();
    Validate(info);
    return nullptr;
  }
  Validate(info);
  return reinterpret_cast<void*>(result.base);
}

bool GtosLowLevelUnmapPages(void* address, size_t size) {
  PageOperation operation;
  const GtosProcessInfoResult info = CurrentProcessInfo();
  Validate(info);
  const uintptr_t base = reinterpret_cast<uintptr_t>(address);
  if (size == 0 || ((base | size) & (GTOS_VM_PAGE_BYTES - 1)) != 0 ||
      base < kRegionBegin || base >= kRegionEnd || size > kRegionEnd - base ||
      base < info.user_begin || base >= info.user_end ||
      size > info.user_end - base) {
    return false;
  }

  unsigned selected[kMaximumRegions] = {};
  unsigned count = 0;
  unsigned cursor = static_cast<unsigned>(base);
  unsigned remaining = static_cast<unsigned>(size);
  while (remaining != 0) {
    unsigned index = kMaximumRegions;
    for (unsigned i = 0; i < info.maximum_regions; ++i) {
      if (regions[i].handle != 0 && regions[i].base == cursor) {
        index = i;
        break;
      }
    }
    if (index == kMaximumRegions || count == kMaximumRegions ||
        regions[index].length > remaining) {
      return false;
    }
    selected[count++] = index;
    cursor += regions[index].length;
    remaining -= regions[index].length;
  }
  for (unsigned i = 0; i < count; ++i) {
    if (!CurrentRegion(regions[selected[i]])) return false;
  }
  for (unsigned i = 0; i < count; ++i) {
    if (!ReleaseRegion(&regions[selected[i]])) return false;
  }
  Validate(info);
  return true;
}

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl
