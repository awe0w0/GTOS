// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdlib.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <new>

#include <process/abi.h>

#include "partition_alloc/partition_alloc.h"
#include "partition_alloc/partition_alloc_constants.h"

namespace {

using partition_alloc::AllocFlags;
using partition_alloc::FreeFlags;
using partition_alloc::PartitionRoot;

constexpr unsigned kHeapFailure = 0x48000000U;
constexpr auto kTryAllocate = AllocFlags::kReturnNull | AllocFlags::kNoHooks;
alignas(PartitionRoot) constinit std::byte root_storage[sizeof(PartitionRoot)];
constinit PartitionRoot* heap = nullptr;

}  // namespace

extern "C" [[noreturn]] void gtos_native_heap_panic(unsigned code) noexcept {
  static constexpr char message[] = "GTOS CONTENT HEAP FAIL\n";
  unsigned call = GTOS_SYS_WRITE;
  asm volatile("int $0x80"
               : "+a"(call)
               : "b"(message), "c"(sizeof(message) - 1)
               : "cc", "memory");
  call = GTOS_SYS_EXIT;
  asm volatile("int $0x80"
               : "+a"(call)
               : "b"(kHeapFailure | code), "c"(0)
               : "cc", "memory");
  __builtin_trap();
}

namespace {

void OutOfMemory(size_t) {
  gtos_native_heap_panic(0x10U);
}

PartitionRoot& Heap() {
  if (!heap) {
    gtos_native_heap_panic(3U);
  }
  return *heap;
}

void* ReportAllocation(void* result) {
  if (!result) {
    errno = ENOMEM;
  }
  return result;
}

}  // namespace

extern "C" int gtos_content_heap_initialize() noexcept {
  if (heap) {
    return EBUSY;
  }
  partition_alloc::PartitionAllocGlobalInit(&OutOfMemory);
  // Install the root before the environment or C++ initializers can allocate.
  // The content process owns it until the kernel reclaims the address space.
  heap = new (root_storage) PartitionRoot(partition_alloc::PartitionOptions());
  return 0;
}

extern "C" void* malloc(size_t size) noexcept {
  return size ? ReportAllocation(Heap().Alloc<kTryAllocate>(size)) : nullptr;
}

extern "C" void* calloc(size_t count, size_t size) noexcept {
  size_t bytes;
  if (__builtin_mul_overflow(count, size, &bytes)) {
    errno = ENOMEM;
    return nullptr;
  }
  if (!bytes) {
    return nullptr;
  }
  return ReportAllocation(
      Heap().Alloc<kTryAllocate | AllocFlags::kZeroFill>(bytes));
}

extern "C" void free(void* pointer) noexcept {
  if (pointer) {
    Heap().Free<FreeFlags::kNoHooks>(pointer);
  }
}

extern "C" void* realloc(void* pointer, size_t size) noexcept {
  if (!size) {
    free(pointer);
    return nullptr;
  }
  return ReportAllocation(
      Heap().Realloc<kTryAllocate>(pointer, size, nullptr));
}

extern "C" int posix_memalign(void** output, size_t alignment,
                             size_t size) noexcept {
  const uintptr_t address = reinterpret_cast<uintptr_t>(output);
  if (address < 0x40000000U || address > 0xc0000000U - sizeof(*output) ||
      address % alignof(void*) || alignment < sizeof(void*) ||
      !std::has_single_bit(alignment)) {
    return EINVAL;
  }
  if (alignment > partition_alloc::internal::kMaxSupportedAlignment) {
    return ENOMEM;
  }
  if (!size) {
    *output = nullptr;
    return 0;
  }
  void* result = alignment <= partition_alloc::internal::kAlignment
                     ? Heap().Alloc<kTryAllocate>(size)
                     : Heap().AlignedAlloc<kTryAllocate>(alignment, size);
  if (!result) {
    return ENOMEM;
  }
  *output = result;
  return 0;
}
