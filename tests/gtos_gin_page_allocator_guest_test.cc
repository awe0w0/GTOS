// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <process/vm_abi.h>

#include "gin/v8_platform_page_allocator.h"
#include "partition_alloc/page_allocator.h"

int TestGinPageAllocator() {
    gin::PageAllocator allocator;
    const size_t page = allocator.CommitPageSize();
    if (page != GTOS_VM_PAGE_BYTES || allocator.AllocatePageSize() != page)
        return 101;
    const size_t baseline = partition_alloc::GetTotalMappedSize();
    constexpr auto read_write = v8::PageAllocator::Permission::kReadWrite;
    void* base = allocator.AllocatePages(nullptr, 3 * page, page, read_write);
    if (!base || partition_alloc::GetTotalMappedSize() != baseline + 3 * page)
        return 102;
    const uintptr_t address = reinterpret_cast<uintptr_t>(base);
    void* tail = reinterpret_cast<void*>(address + 2 * page);
    auto* bytes = static_cast<volatile unsigned char*>(base);
    bytes[0] = 19;
    bytes[2 * page - 1] = 73;
    bytes[2 * page] = 211;
    if (allocator.ReleasePages(tail, 3 * page, page) ||
        allocator.ReleasePages(base, 2 * page, page) ||
        allocator.ReleasePages(base, 3 * page, page + 1) ||
        allocator.ReleasePages(base, 3 * page, 4 * page) ||
        partition_alloc::GetTotalMappedSize() != baseline + 3 * page ||
        bytes[2 * page] != 211)
        return 103;
    if (!allocator.ReleasePages(base, 3 * page, 2 * page) ||
        partition_alloc::GetTotalMappedSize() != baseline + 2 * page ||
        bytes[0] != 19 || bytes[2 * page - 1] != 73 ||
        allocator.SetPermissions(tail, page, read_write))
        return 104;
    if (allocator.AllocatePages(tail, page, page, read_write) != tail ||
        *static_cast<volatile unsigned char*>(tail) != 0 ||
        partition_alloc::GetTotalMappedSize() != baseline + 3 * page)
        return 105;
    if (!allocator.ReleasePages(tail, page, 0) ||
        allocator.ReleasePages(tail, page, 0) ||
        partition_alloc::GetTotalMappedSize() != baseline + 2 * page ||
        !allocator.FreePages(base, 2 * page) ||
        partition_alloc::GetTotalMappedSize() != baseline ||
        allocator.SetPermissions(base, page, read_write))
        return 106;
    return 0;
}
