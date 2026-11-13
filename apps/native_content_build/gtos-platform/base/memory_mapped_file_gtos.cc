// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/memory_mapped_file.h"
#include "base/files/gtos_file_snapshot.h"

#include <algorithm>
#include <limits>

#include "base/compiler_specific.h"
#include "base/numerics/safe_conversions.h"
#include "base/threading/scoped_blocking_call.h"
#include "partition_alloc/page_allocator.h"

namespace base {
namespace {

using PageAccess = partition_alloc::PageAccessibilityConfiguration;

// The GTOS data VM permits at most 256 pages per commit or permission change.
constexpr size_t kCommitBytes = 256 * 4096;
constexpr size_t kGranularity =
    partition_alloc::internal::PageAllocationGranularity();
static_assert(kGranularity == 4096);

size_t MappingSize(size_t size) {
  return (size + (kGranularity - 1)) & ~(kGranularity - 1);
}

}  // namespace

span<uint8_t> MapGtosFileSnapshot(File& file, int64_t file_offset, size_t size,
                                 bool readonly) {
  if (!size || file_offset < 0 ||
      size > static_cast<uint64_t>(std::numeric_limits<int64_t>::max() - file_offset) ||
      size > std::numeric_limits<size_t>::max() - (kGranularity - 1)) {
    return {};
  }
  const size_t capacity = MappingSize(size);
  const uintptr_t address = partition_alloc::AllocPages(
      capacity, kGranularity, PageAccess(PageAccess::kInaccessible));
  if (!address) {
    return {};
  }
  // SAFETY: AllocPages reserved capacity bytes at this page-aligned base.
  // Each subrange becomes accessible before being passed to File::Read.
  const auto target =
      UNSAFE_BUFFERS(span(reinterpret_cast<uint8_t*>(address), size));
  for (size_t offset = 0; offset < capacity;) {
    const size_t chunk = std::min(kCommitBytes, capacity - offset);
    const auto buffer = target.subspan(offset, std::min(chunk, size - offset));
    if (!partition_alloc::TrySetSystemPagesAccess(
            address + offset, chunk, PageAccess(PageAccess::kReadWrite)) ||
        file.Read(file_offset + offset, buffer) != buffer.size() ||
        (readonly && !partition_alloc::TrySetSystemPagesAccess(
                         address + offset, chunk, PageAccess(PageAccess::kRead)))) {
      partition_alloc::FreePages(address, capacity);
      return {};
    }
    offset += chunk;
  }
  return target;
}

void UnmapGtosFileSnapshot(const void* address, size_t size) {
  if (address && size) {
    partition_alloc::FreePages(reinterpret_cast<uintptr_t>(address), MappingSize(size));
  }
}

MemoryMappedFile::MemoryMappedFile() = default;

bool MemoryMappedFile::MapFileRegionToMemory(const Region& region,
                                             Access access) {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  // A GTOS mapping is a private snapshot. Shared writes require a file-backed
  // mapping service, which the native VM does not provide.
  if (access != READ_ONLY && access != READ_WRITE_COPY) {
    return false;
  }
  const int64_t length = file_.GetLength();
  if (length <= 0 || region.offset < 0 || region.offset >= length) {
    return false;
  }
  const int64_t remaining = length - region.offset;
  const bool whole_file = region == Region::kWholeFile;
  if (whole_file && !IsValueInRangeForNumericType<size_t>(remaining)) {
    return false;
  }
  const size_t size = whole_file ? static_cast<size_t>(remaining) : region.size;
  if (!size || static_cast<uint64_t>(size) > static_cast<uint64_t>(remaining)) {
    return false;
  }
  bytes_ = MapGtosFileSnapshot(file_, region.offset, size, access == READ_ONLY);
  return !bytes_.empty();
}

void MemoryMappedFile::CloseHandles() {
  ScopedBlockingCall blocking(FROM_HERE, BlockingType::MAY_BLOCK);
  UnmapGtosFileSnapshot(bytes_.data(), bytes_.size());
  bytes_ = {};
  file_.Close();
}

}  // namespace base
