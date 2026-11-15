// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PARTITION_ALLOC_PAGE_ALLOCATOR_INTERNALS_GTOS_H_
#define PARTITION_ALLOC_PAGE_ALLOCATOR_INTERNALS_GTOS_H_

#include <atomic>
#include <cerrno>

#include <process/vm_abi.h>

#include "partition_alloc/internal/page_allocator_internal.h"
#include "partition_alloc/page_allocator.h"
#include "partition_alloc/partition_alloc_base/logging.h"
#include "partition_alloc/partition_alloc_base/strings/cstring_builder.h"
#include "partition_alloc/partition_alloc_check.h"
#include "partition_alloc/partition_lock.h"

#if !PA_BUILDFLAG(IS_GTOS) || !defined(__i386__)
#error "This provider requires the GTOS IA32 VM ABI."
#endif

namespace partition_alloc::internal {
namespace {

static_assert(sizeof(uintptr_t) == sizeof(unsigned));
static_assert(SystemPageSize() == GTOS_VM_PAGE_BYTES);

struct Reservation {
  uintptr_t address;
  size_t length;
  unsigned handle;
};

// Match the Content profile's kernel limit in the allocation-free index.
// Serialize lookups with release so a recycled handle cannot be used.
Lock g_vm_lock;
Reservation g_reservations[GTOS_VM_CONTENT_REGION_LIMIT] PA_GUARDED_BY(g_vm_lock);

void ReportAllocationFailure(const char* phase, int status, size_t length,
                             PageTag tag) {
  if (tag != PageTag::kV8 && status != GTOS_VM_ERR_LIMIT &&
      status != GTOS_VM_ERR_NO_MEMORY) {
    return;
  }
  // Page allocation failure can make allocating a log string impossible.
  base::strings::CStringBuilder message;
  message << (tag == PageTag::kV8 ? "GTOS V8 page allocation "
                                 : "GTOS PartitionAlloc page allocation ")
          << phase << " failed: status="
          << status << " bytes=" << length;
  PA_RAW_LOG(ERROR, message.c_str());
}

template <typename Request>
int VmCall(unsigned operation, const Request& request) {
  asm volatile("int $0x80"
               : "+a"(operation)
               : "b"(&request), "c"(sizeof(request))
               : "memory", "cc");
  return static_cast<int>(operation);
}

int VmProtection(PageAccessibilityConfiguration access) {
  switch (access.permissions) {
    case PageAccessibilityConfiguration::kInaccessible:
      return GTOS_VM_NONE;
    case PageAccessibilityConfiguration::kRead:
      return GTOS_VM_READ;
    case PageAccessibilityConfiguration::kReadWrite:
    case PageAccessibilityConfiguration::kReadWriteTagged:
      return GTOS_VM_READ_WRITE;
    default:
      // The data VM ABI cannot grant execute permission.
      return GTOS_VM_ERR_PERMISSION;
  }
}

int VmRange(unsigned operation, uintptr_t address, size_t length,
            unsigned protection) {
  if (!length || ((address | length) & (GTOS_VM_PAGE_BYTES - 1))) {
    return GTOS_VM_ERR_BAD_SIZE;
  }
  ScopedGuard guard(g_vm_lock);
  for (const auto& entry : g_reservations) {
    if (entry.handle && address >= entry.address && length <= entry.length &&
        address - entry.address <= entry.length - length) {
      const GtosVmRangeRequest request{
          GTOS_VM_ABI_VERSION, entry.handle,
          static_cast<unsigned>(address - entry.address),
          static_cast<unsigned>(length), protection};
      return VmCall(operation, request);
    }
  }
  return GTOS_VM_ERR_RANGE;
}

int VmRelease(unsigned handle) {
  const GtosVmControlRequest request{GTOS_VM_ABI_VERSION, handle, 0};
  return VmCall(GTOS_SYS_VM_RELEASE, request);
}

}  // namespace

// The kernel treats a nonzero hint as an exact address.
constexpr bool kHintIsAdvisory = false;
std::atomic<int32_t> s_allocPageErrorCode{0};

uintptr_t SystemAllocPagesInternal(uintptr_t hint,
                                   size_t length,
                                   PageAccessibilityConfiguration access,
                                   PageTag tag,
                                   int file_descriptor) {
  const int protection = VmProtection(access);
  if (protection < 0 || file_descriptor != -1) {
    const int status = protection < 0 ? protection : -ENOTSUP;
    s_allocPageErrorCode = -status;
    ReportAllocationFailure("protection", status, length, tag);
    return 0;
  }

  ScopedGuard guard(g_vm_lock);
  for (auto& entry : g_reservations) {
    if (entry.handle) {
      continue;
    }
    GtosVmReserveResult result{};
    const GtosVmReserveRequest request{
        GTOS_VM_ABI_VERSION, static_cast<unsigned>(length), GTOS_VM_PAGE_BYTES,
        static_cast<unsigned>(hint),
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(&result))};
    int status = VmCall(GTOS_SYS_VM_RESERVE, request);
    if (status != 0) {
      s_allocPageErrorCode = -status;
      if (!hint)
        ReportAllocationFailure("reserve", status, length, tag);
      return 0;
    }
    PA_CHECK(result.version == GTOS_VM_ABI_VERSION && result.handle &&
             result.base && !(result.base & (GTOS_VM_PAGE_BYTES - 1)) &&
             result.length == length && result.page_size == GTOS_VM_PAGE_BYTES &&
             result.base <= UINT32_MAX - length &&
             (!hint || result.base == hint));
    if (protection != GTOS_VM_NONE) {
      const GtosVmRangeRequest commit{GTOS_VM_ABI_VERSION, result.handle, 0,
                                      static_cast<unsigned>(length),
                                      static_cast<unsigned>(protection)};
      status = VmCall(GTOS_SYS_VM_SET_PERMISSIONS, commit);
      if (status != 0) {
        PA_CHECK(VmRelease(result.handle) == 0);
        s_allocPageErrorCode = -status;
        ReportAllocationFailure("commit", status, length, tag);
        return 0;
      }
    }
    entry = {result.base, length, result.handle};
    return result.base;
  }
  s_allocPageErrorCode = -GTOS_VM_ERR_LIMIT;
  ReportAllocationFailure("index", GTOS_VM_ERR_LIMIT, length, tag);
  return 0;
}

void FreePagesInternal(uintptr_t address, size_t length) {
  ScopedGuard guard(g_vm_lock);
  for (auto& entry : g_reservations) {
    if (entry.handle && entry.address == address && entry.length == length) {
      PA_CHECK(VmRelease(entry.handle) == 0);
      entry = {};
      return;
    }
  }
  PA_CHECK(false);
}

bool TryShrinkPagesInternal(uintptr_t address, size_t length,
                            size_t new_length) {
  if (new_length >= length || (new_length & (GTOS_VM_PAGE_BYTES - 1))) {
    return false;
  }
  ScopedGuard guard(g_vm_lock);
  for (auto& entry : g_reservations) {
    if (!entry.handle || entry.address != address || entry.length != length) {
      continue;
    }
    const GtosVmControlRequest request{
        GTOS_VM_ABI_VERSION, entry.handle, static_cast<unsigned>(new_length)};
    const unsigned operation = new_length ? GTOS_SYS_VM_TRIM : GTOS_SYS_VM_RELEASE;
    if (VmCall(operation, request) != 0) {
      return false;
    }
    if (new_length) {
      entry.length = new_length;
    } else {
      entry = {};
    }
    return true;
  }
  return false;
}

uintptr_t TrimMappingInternal(uintptr_t base_address,
                              size_t base_length,
                              size_t trim_length,
                              PageAccessibilityConfiguration access,
                              size_t pre_slack,
                              size_t post_slack) {
  if (!pre_slack && !post_slack) {
    return base_address;
  }
  // Like Windows, release and reserve the exact subrange before publishing
  // the new address. The caller retries if a competing reservation wins.
  FreePages(base_address, base_length);
  return SystemAllocPages(base_address + pre_slack, trim_length, access,
                          PageTag::kChromium, -1);
}

bool TrySetSystemPagesAccessInternal(uintptr_t address,
                                     size_t length,
                                     PageAccessibilityConfiguration access) {
  const int protection = VmProtection(access);
  return protection >= 0 &&
         VmRange(GTOS_SYS_VM_SET_PERMISSIONS, address, length,
                 static_cast<unsigned>(protection)) == 0;
}

void SetSystemPagesAccessInternal(uintptr_t address,
                                  size_t length,
                                  PageAccessibilityConfiguration access) {
  PA_CHECK(TrySetSystemPagesAccessInternal(address, length, access));
}

bool DecommitAndZeroSystemPagesInternal(uintptr_t address,
                                        size_t length,
                                        PageTag) {
  return VmRange(GTOS_SYS_VM_DECOMMIT, address, length, 0) == 0;
}

void DecommitSystemPagesInternal(uintptr_t address,
                                 size_t length,
                                 PageAccessibilityDisposition) {
  PA_CHECK(DecommitAndZeroSystemPagesInternal(address, length, PageTag::kChromium));
}

bool TryRecommitSystemPagesInternal(uintptr_t address,
                                    size_t length,
                                    PageAccessibilityConfiguration access,
                                    PageAccessibilityDisposition) {
  return access.permissions != PageAccessibilityConfiguration::kInaccessible &&
         TrySetSystemPagesAccessInternal(address, length, access);
}

void RecommitSystemPagesInternal(uintptr_t address,
                                 size_t length,
                                 PageAccessibilityConfiguration access,
                                 PageAccessibilityDisposition disposition) {
  PA_CHECK(TryRecommitSystemPagesInternal(address, length, access, disposition));
}

void DiscardSystemPagesInternal(uintptr_t address, size_t length) {
  PA_CHECK(VmRange(GTOS_SYS_VM_DISCARD, address, length, 0) == 0);
}

bool SealSystemPagesInternal(uintptr_t, size_t) {
  return false;
}

}  // namespace partition_alloc::internal

#endif  // PARTITION_ALLOC_PAGE_ALLOCATOR_INTERNALS_GTOS_H_
