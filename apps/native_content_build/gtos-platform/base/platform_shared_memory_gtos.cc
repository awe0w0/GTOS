// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/platform_shared_memory_mapper.h"
#include "base/memory/platform_shared_memory_region.h"

#include <limits>
#include <utility>

#include <process/shared_memory_abi.h>

#include "base/check_op.h"
#include "base/compiler_specific.h"
#include "base/memory/page_size.h"
#include "base/notreached.h"
#include "base/system/sys_info.h"

namespace base {
namespace {

template<class Request>
int Call(unsigned operation, const Request& request) {
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(&request), "c"(sizeof(request)) : "memory", "cc");
  return static_cast<int>(operation);
}

unsigned NativeMode(subtle::PlatformSharedMemoryRegion::Mode mode) {
  using Mode = subtle::PlatformSharedMemoryRegion::Mode;
  switch (mode) {
    case Mode::kReadOnly:
      return GTOS_SHARED_MEMORY_READ_ONLY;
    case Mode::kWritable:
      return GTOS_SHARED_MEMORY_WRITABLE;
    case Mode::kUnsafe:
      return GTOS_SHARED_MEMORY_UNSAFE;
  }
  NOTREACHED();
}

bool Convert(subtle::ScopedPlatformSharedMemoryHandle& handle,
             subtle::PlatformSharedMemoryRegion::Mode mode) {
  const GtosSharedMemoryConvertRequest request{GTOS_SHARED_MEMORY_ABI_VERSION,
                                               handle.get(), NativeMode(mode)};
  if (Call(GTOS_SYS_SHARED_MEMORY_CONVERT, request) == 0) {
    return true;
  }
  handle.reset();
  return false;
}

}  // namespace

size_t GetPageSize() {
  return GTOS_SHARED_MEMORY_PAGE_BYTES;
}

size_t SysInfo::VMAllocationGranularity() {
  return GetPageSize();
}

std::optional<span<uint8_t>> PlatformSharedMemoryMapper::Map(
    subtle::PlatformSharedMemoryHandle handle,
    bool write_allowed,
    uint64_t offset,
    size_t size) {
  if (offset > std::numeric_limits<unsigned>::max()) {
    return std::nullopt;
  }
  unsigned address = 0;
  const GtosSharedMemoryMapRequest request{
      GTOS_SHARED_MEMORY_ABI_VERSION, handle, static_cast<unsigned>(offset),
      size, write_allowed, reinterpret_cast<unsigned>(&address)};
  if (Call(GTOS_SYS_SHARED_MEMORY_MAP, request) != 0) {
    return std::nullopt;
  }
  CHECK_NE(address, 0u);
  return UNSAFE_TODO(span(reinterpret_cast<uint8_t*>(address), size));
}

void PlatformSharedMemoryMapper::Unmap(span<uint8_t> mapping) {
  // MapAt() may have removed a prefix to expose a non-page-aligned offset.
  const unsigned address = reinterpret_cast<unsigned>(mapping.data());
  const unsigned prefix = address % GTOS_SHARED_MEMORY_PAGE_BYTES;
  const GtosSharedMemoryUnmapRequest request{GTOS_SHARED_MEMORY_ABI_VERSION,
      address - prefix, mapping.size() + prefix};
  CHECK_EQ(Call(GTOS_SYS_SHARED_MEMORY_UNMAP, request), 0);
}

namespace subtle {

void GtosSharedMemoryHandleTraits::Free(PlatformSharedMemoryHandle handle) {
  const GtosSharedMemoryControlRequest request{GTOS_SHARED_MEMORY_ABI_VERSION, handle};
  const int result = Call(GTOS_SYS_SHARED_MEMORY_CLOSE, request);
  // Adopting an invalid capability may fail before ownership is established.
  CHECK(result == 0 || result == GTOS_SHARED_MEMORY_ERR_BAD_HANDLE ||
        result == GTOS_SHARED_MEMORY_ERR_UNAVAILABLE);
}

expected<PlatformSharedMemoryRegion, PlatformSharedMemoryRegion::TakeError>
PlatformSharedMemoryRegion::TakeOrFail(ScopedPlatformSharedMemoryHandle handle,
                                       Mode mode,
                                       size_t size,
                                       const UnguessableToken& guid) {
  if (!handle.is_valid() || !size || size > GTOS_SHARED_MEMORY_MAX_BYTES) {
    return {};
  }
  return CheckPlatformHandlePermissionsCorrespondToMode(handle.get(), mode, size)
      .transform([&] {
        return PlatformSharedMemoryRegion(std::move(handle), mode, size, guid);
      });
}

PlatformSharedMemoryHandle PlatformSharedMemoryRegion::GetPlatformHandle() const {
  return handle_.get();
}

bool PlatformSharedMemoryRegion::IsValid() const {
  return handle_.is_valid();
}

PlatformSharedMemoryRegion PlatformSharedMemoryRegion::Duplicate() const {
  if (!IsValid()) {
    return {};
  }
  CHECK_NE(mode_, Mode::kWritable);
  const GtosSharedMemoryControlRequest request{GTOS_SHARED_MEMORY_ABI_VERSION,
                                               handle_.get()};
  const int result = Call(GTOS_SYS_SHARED_MEMORY_DUP, request);
  if (result <= 0) {
    return {};
  }
  return PlatformSharedMemoryRegion(
      ScopedPlatformSharedMemoryHandle(static_cast<unsigned>(result)), mode_, size_, guid_);
}

bool PlatformSharedMemoryRegion::ConvertToReadOnly() {
  if (!IsValid()) {
    return false;
  }
  CHECK_EQ(mode_, Mode::kWritable);
  if (!Convert(handle_, Mode::kReadOnly)) {
    return false;
  }
  mode_ = Mode::kReadOnly;
  return true;
}

bool PlatformSharedMemoryRegion::ConvertToUnsafe() {
  if (!IsValid()) {
    return false;
  }
  CHECK_EQ(mode_, Mode::kWritable);
  if (!Convert(handle_, Mode::kUnsafe)) {
    return false;
  }
  mode_ = Mode::kUnsafe;
  return true;
}

PlatformSharedMemoryRegion PlatformSharedMemoryRegion::Create(Mode mode, size_t size) {
  CHECK_NE(mode, Mode::kReadOnly);
  if (!size || size > GTOS_SHARED_MEMORY_MAX_BYTES) {
    return {};
  }
  const GtosSharedMemoryCreateRequest request{GTOS_SHARED_MEMORY_ABI_VERSION,
                                              size, NativeMode(mode)};
  const int result = Call(GTOS_SYS_SHARED_MEMORY_CREATE, request);
  if (result <= 0) {
    return {};
  }
  return PlatformSharedMemoryRegion(
      ScopedPlatformSharedMemoryHandle(static_cast<unsigned>(result)), mode, size,
      UnguessableToken::Create());
}

expected<void, PlatformSharedMemoryRegion::TakeError>
PlatformSharedMemoryRegion::CheckPlatformHandlePermissionsCorrespondToMode(
    PlatformSharedMemoryHandle handle,
    Mode mode,
    size_t size) {
  GtosSharedMemoryInfo info{};
  const GtosSharedMemoryInfoRequest request{GTOS_SHARED_MEMORY_ABI_VERSION,
      handle, reinterpret_cast<unsigned>(&info)};
  if (Call(GTOS_SYS_SHARED_MEMORY_INFO, request) != 0 ||
      info.version != GTOS_SHARED_MEMORY_ABI_VERSION) {
    return unexpected(TakeError::kInvalidGtosHandle);
  }
  if (info.bytes != size) {
    return unexpected(TakeError::kGtosSizeMismatch);
  }
  if (info.mode != NativeMode(mode)) {
    return unexpected(TakeError::kGtosModeMismatch);
  }
  return ok();
}

PlatformSharedMemoryRegion::PlatformSharedMemoryRegion(
    ScopedPlatformSharedMemoryHandle handle,
    Mode mode,
    size_t size,
    const UnguessableToken& guid)
    : handle_(std::move(handle)), mode_(mode), size_(size), guid_(guid) {}

}  // namespace subtle
}  // namespace base
