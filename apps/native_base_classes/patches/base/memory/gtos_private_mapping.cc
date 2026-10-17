// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/gtos_private_mapping.h"

#if BUILDFLAG(IS_GTOS)

#include <stdint.h>
#include <process/vm_abi.h>

#include <new>

#include "base/check.h"

namespace base::internal {
namespace {

constexpr uint32_t kPage = GTOS_VM_PAGE_BYTES;
// Native VM commits are limited to 256 pages per transaction, even when the
// current process has a larger total resident-page budget.
constexpr uint32_t kCommitBytes = 256 * kPage;
constexpr uint32_t kMagic = 0x504d4156;
constexpr uint32_t kUserBegin = 0x40000000;
constexpr uint32_t kUserEnd = 0xc0000000;

struct MappingHeader {
  uint32_t magic;
  uint32_t handle;
  uint32_t requested;
  uint32_t reserved;
};

static_assert(sizeof(uintptr_t) == 4);
static_assert(sizeof(MappingHeader) <= kPage);

template <typename Request>
int Call(uint32_t operation, Request& request) {
  asm volatile("int $0x80"
               : "+a"(operation)
               : "b"(reinterpret_cast<uintptr_t>(&request)),
                 "c"(sizeof(request))
               : "memory", "cc");
  return static_cast<int>(operation);
}

uint32_t ReservationBytes(size_t size) {
  if (size == 0 || size > UINT32_MAX - 2 * kPage + 1) {
    return 0;
  }
  return ((static_cast<uint32_t>(size) + kPage - 1) & ~(kPage - 1)) +
         kPage;
}

void Release(uint32_t handle) {
  GtosVmControlRequest request = {GTOS_VM_ABI_VERSION, handle, 0};
  RAW_CHECK(Call(GTOS_SYS_VM_RELEASE, request) == 0);
}

}  // namespace

void* GtosAllocatePrivateMapping(size_t size) {
  const uint32_t length = ReservationBytes(size);
  if (length == 0) {
    return nullptr;
  }

  GtosVmReserveResult result = {};
  GtosVmReserveRequest reserve = {
      GTOS_VM_ABI_VERSION, length, kPage, 0,
      reinterpret_cast<uintptr_t>(&result)};
  if (Call(GTOS_SYS_VM_RESERVE, reserve) != 0) {
    return nullptr;
  }
  // Mapping failures and invariant checks must not allocate through logging.
  RAW_CHECK(result.version == GTOS_VM_ABI_VERSION && result.handle != 0 &&
        result.base >= kUserBegin && (result.base & (kPage - 1)) == 0 &&
        result.length == length && result.page_size == kPage &&
        static_cast<uint64_t>(result.base) + length <= kUserEnd);

  GtosVmRangeRequest range = {GTOS_VM_ABI_VERSION, result.handle, 0, 0,
                             GTOS_VM_READ_WRITE};
  while (range.offset < length) {
    const uint32_t remaining = length - range.offset;
    range.length = remaining < kCommitBytes ? remaining : kCommitBytes;
    if (Call(GTOS_SYS_VM_SET_PERMISSIONS, range) != 0) {
      Release(result.handle);
      return nullptr;
    }
    range.offset += range.length;
  }

  auto* header = reinterpret_cast<MappingHeader*>(result.base);
  new (header)
      MappingHeader{kMagic, result.handle, static_cast<uint32_t>(size), length};
  range.offset = 0;
  range.length = kPage;
  range.protection = GTOS_VM_READ;
  if (Call(GTOS_SYS_VM_SET_PERMISSIONS, range) != 0) {
    Release(result.handle);
    return nullptr;
  }

  return reinterpret_cast<void*>(result.base + kPage);
}

void GtosReleasePrivateMapping(void* memory, size_t size) {
  const uint32_t length = ReservationBytes(size);
  const uintptr_t address = reinterpret_cast<uintptr_t>(memory);
  RAW_CHECK(length != 0 && address >= kUserBegin + kPage && address < kUserEnd &&
        (address & (kPage - 1)) == 0);
  const uintptr_t base = address - kPage;
  const MappingHeader header = *reinterpret_cast<const MappingHeader*>(base);
  RAW_CHECK(header.magic == kMagic && header.handle != 0 &&
        header.requested == size && header.reserved == length);

  GtosVmRegionInfo info = {};
  GtosVmQueryRequest query = {GTOS_VM_ABI_VERSION, header.handle,
                             reinterpret_cast<uintptr_t>(&info)};
  RAW_CHECK(Call(GTOS_SYS_VM_QUERY, query) == 0 &&
        info.version == GTOS_VM_ABI_VERSION && info.handle == header.handle &&
        info.base == base && info.length == length);
  Release(header.handle);
}

}  // namespace base::internal

#endif  // BUILDFLAG(IS_GTOS)
