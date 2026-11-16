#include <process/native_shared_memory.h>
#include <process/shared_memory_abi.h>
#include <memory/criticalsection.h>

using namespace gtos::process;
using gtos::memory::InterruptGuard;
using gtos::memory::PhysicalMemoryManager;
using gtos::memory::ProcessAddressSpace;

namespace {
    const uint32_t PageBytes = GTOS_SHARED_MEMORY_PAGE_BYTES;

    uint32_t Pages(uint32_t bytes) {
        return (bytes + PageBytes - 1) / PageBytes;
    }

    template<class Request>
    int32_t Snapshot(ProcessAddressSpace& space, uint32_t address,
                     uint32_t bytes, Request& request) {
        if (bytes != sizeof(request)) return GTOS_SHARED_MEMORY_ERR_INVALID;
        if (!space.CopyFromUser(&request, address, bytes)) return GTOS_SHARED_MEMORY_ERR_BAD_ADDRESS;
        return request.version == GTOS_SHARED_MEMORY_ABI_VERSION ? 0 : GTOS_SHARED_MEMORY_ERR_UNAVAILABLE;
    }

    int32_t MapError(gtos::memory::ProcessMemoryError error) {
        using namespace gtos::memory;
        if (error == ProcessMemoryNoMemory) return GTOS_SHARED_MEMORY_ERR_NO_MEMORY;
        if (error == ProcessMemoryHandleExhausted) return GTOS_SHARED_MEMORY_ERR_OVERFLOW;
        if (error == ProcessMemoryLimit || error == ProcessMemoryRegionLimit)
            return GTOS_SHARED_MEMORY_ERR_LIMIT;
        return GTOS_SHARED_MEMORY_ERR_INVALID;
    }
}

NativeSharedMemory::NativeSharedMemory(PhysicalMemoryManager& allocator)
    : frames(allocator), objects(), handles(), mappings(), nextHandle(1),
      objectLimit(MaximumObjects), handleLimit(MaximumHandles), mappingLimit(MaximumMappings) {}

bool NativeSharedMemory::EnableContentProfile() {
    InterruptGuard guard;
    if (nextHandle != 1 || objectLimit != MaximumObjects) return false;
    objectLimit = ContentMaximumObjects;
    handleLimit = ContentMaximumHandles;
    mappingLimit = ContentMaximumMappings;
    return true;
}

NativeSharedMemory::Handle* NativeSharedMemory::FindHandle(uint32_t owner, uint32_t id) {
    if (!id) return 0;
    for (uint32_t i = 0; i < handleLimit; ++i)
        if (handles[i].id == id && handles[i].owner == owner) return &handles[i];
    return 0;
}

void NativeSharedMemory::DropReference(Object& object) {
    if (--object.references == 0) {
        frames.freeContiguous(object.firstFrame, Pages(object.bytes));
        object = {};
    }
}

int32_t NativeSharedMemory::Create(uint32_t owner, ProcessAddressSpace& space,
                                   uint32_t bytes, uint32_t mode) {
    if (!bytes || bytes > GTOS_SHARED_MEMORY_MAX_BYTES ||
        (mode != GTOS_SHARED_MEMORY_WRITABLE && mode != GTOS_SHARED_MEMORY_UNSAFE))
        return GTOS_SHARED_MEMORY_ERR_INVALID;
    if (nextHandle > 0x7fffffffU) return GTOS_SHARED_MEMORY_ERR_OVERFLOW;
    Object* object = 0;
    Handle* handle = 0;
    for (uint32_t i = 0; i < objectLimit; ++i)
        if (!objects[i].references) { object = &objects[i]; break; }
    for (uint32_t i = 0; i < handleLimit; ++i)
        if (!handles[i].id) { handle = &handles[i]; break; }
    if (!object || !handle) return GTOS_SHARED_MEMORY_ERR_LIMIT;

    const uint32_t pages = Pages(bytes);
    uint32_t firstFrame = 0;
    if (!frames.allocateContiguous(pages, firstFrame)) return GTOS_SHARED_MEMORY_ERR_NO_MEMORY;
    if (!space.KernelAlias(firstFrame, pages * PageBytes, true)) {
        frames.freeContiguous(firstFrame, pages);
        return GTOS_SHARED_MEMORY_ERR_NO_MEMORY;
    }
    // Zero the complete last page as well. Users can read its padding.
    uint8_t* data = reinterpret_cast<uint8_t*>(firstFrame);
    for (uint32_t i = 0; i < pages * PageBytes; ++i) data[i] = 0;
    *object = {firstFrame, bytes, mode, 1};
    *handle = {nextHandle++, owner, object};
    return static_cast<int32_t>(handle->id);
}

int32_t NativeSharedMemory::Duplicate(uint32_t owner, Object& object) {
    if (object.mode == GTOS_SHARED_MEMORY_WRITABLE) return GTOS_SHARED_MEMORY_ERR_PERMISSION;
    if (nextHandle > 0x7fffffffU) return GTOS_SHARED_MEMORY_ERR_OVERFLOW;
    for (uint32_t i = 0; i < handleLimit; ++i) {
        Handle& handle = handles[i];
        if (handle.id) continue;
        handle = {nextHandle++, owner, &object};
        ++object.references;
        return static_cast<int32_t>(handle.id);
    }
    return GTOS_SHARED_MEMORY_ERR_LIMIT;
}

int32_t NativeSharedMemory::Map(uint32_t owner, ProcessAddressSpace& space,
                                uint32_t handle, uint32_t offset, uint32_t bytes,
                                uint32_t writable, uint32_t result) {
    Handle* entry = FindHandle(owner, handle);
    if (!entry) return GTOS_SHARED_MEMORY_ERR_BAD_HANDLE;
    Object& object = *entry->object;
    if (!bytes || offset % PageBytes || offset >= object.bytes ||
        bytes > object.bytes - offset || writable > 1) return GTOS_SHARED_MEMORY_ERR_INVALID;
    if (writable && object.mode == GTOS_SHARED_MEMORY_READ_ONLY)
        return GTOS_SHARED_MEMORY_ERR_PERMISSION;
    if (!space.ValidateUserRange(result, sizeof(uint32_t), true))
        return GTOS_SHARED_MEMORY_ERR_BAD_ADDRESS;
    Mapping* mapping = 0;
    for (uint32_t i = 0; i < mappingLimit; ++i)
        if (!mappings[i].owner) { mapping = &mappings[i]; break; }
    if (!mapping) return GTOS_SHARED_MEMORY_ERR_LIMIT;

    uint32_t base = 0, region = 0;
    if (!space.MapSharedFrames(object.firstFrame + offset, Pages(bytes) * PageBytes,
                               writable != 0, base, region))
        return MapError(space.GetLastError());
    *mapping = {owner, base, bytes, region, &object};
    ++object.references;
    if (!space.CopyToUser(result, &base, sizeof(base))) {
        Unmap(owner, space, base, bytes);
        return GTOS_SHARED_MEMORY_ERR_BAD_ADDRESS;
    }
    return 0;
}

int32_t NativeSharedMemory::Unmap(uint32_t owner, ProcessAddressSpace& space,
                                  uint32_t base, uint32_t bytes) {
    for (uint32_t i = 0; i < mappingLimit; ++i) {
        Mapping& mapping = mappings[i];
        if (mapping.owner != owner || mapping.base != base) continue;
        if (mapping.bytes != bytes) return GTOS_SHARED_MEMORY_ERR_INVALID;
        if (!space.UnmapSharedFrames(mapping.region)) return MapError(space.GetLastError());
        DropReference(*mapping.object);
        mapping = {};
        return 0;
    }
    return GTOS_SHARED_MEMORY_ERR_BAD_HANDLE;
}

int32_t NativeSharedMemory::Call(uint32_t owner, ProcessAddressSpace& space,
                                 uint32_t operation, uint32_t address, uint32_t bytes) {
    InterruptGuard guard;
    if (!owner) return GTOS_SHARED_MEMORY_ERR_INVALID;
    switch (operation) {
    case GTOS_SYS_SHARED_MEMORY_CREATE: {
        GtosSharedMemoryCreateRequest request;
        const int32_t error = Snapshot(space, address, bytes, request);
        return error ? error : Create(owner, space, request.bytes, request.mode);
    }
    case GTOS_SYS_SHARED_MEMORY_CLOSE:
    case GTOS_SYS_SHARED_MEMORY_DUP: {
        GtosSharedMemoryControlRequest request;
        const int32_t error = Snapshot(space, address, bytes, request);
        if (error) return error;
        Handle* handle = FindHandle(owner, request.handle);
        if (!handle) return GTOS_SHARED_MEMORY_ERR_BAD_HANDLE;
        if (operation == GTOS_SYS_SHARED_MEMORY_DUP) return Duplicate(owner, *handle->object);
        DropReference(*handle->object);
        *handle = {};
        return 0;
    }
    case GTOS_SYS_SHARED_MEMORY_CONVERT: {
        GtosSharedMemoryConvertRequest request;
        const int32_t error = Snapshot(space, address, bytes, request);
        if (error) return error;
        Handle* handle = FindHandle(owner, request.handle);
        if (!handle) return GTOS_SHARED_MEMORY_ERR_BAD_HANDLE;
        if (request.mode != GTOS_SHARED_MEMORY_READ_ONLY && request.mode != GTOS_SHARED_MEMORY_UNSAFE)
            return GTOS_SHARED_MEMORY_ERR_INVALID;
        if (handle->object->mode != GTOS_SHARED_MEMORY_WRITABLE)
            return GTOS_SHARED_MEMORY_ERR_PERMISSION;
        handle->object->mode = request.mode;
        return 0;
    }
    case GTOS_SYS_SHARED_MEMORY_INFO: {
        GtosSharedMemoryInfoRequest request;
        const int32_t error = Snapshot(space, address, bytes, request);
        if (error) return error;
        Handle* handle = FindHandle(owner, request.handle);
        if (!handle) return GTOS_SHARED_MEMORY_ERR_BAD_HANDLE;
        const GtosSharedMemoryInfo info{GTOS_SHARED_MEMORY_ABI_VERSION, handle->object->bytes,
            handle->object->mode};
        return space.CopyToUser(request.result, &info, sizeof(info))
            ? 0 : GTOS_SHARED_MEMORY_ERR_BAD_ADDRESS;
    }
    case GTOS_SYS_SHARED_MEMORY_MAP: {
        GtosSharedMemoryMapRequest request;
        const int32_t error = Snapshot(space, address, bytes, request);
        return error ? error : Map(owner, space, request.handle, request.offset,
                                   request.bytes, request.writable, request.result);
    }
    case GTOS_SYS_SHARED_MEMORY_UNMAP: {
        GtosSharedMemoryUnmapRequest request;
        const int32_t error = Snapshot(space, address, bytes, request);
        return error ? error : Unmap(owner, space, request.base, request.bytes);
    }
    }
    return GTOS_SHARED_MEMORY_ERR_UNAVAILABLE;
}

void NativeSharedMemory::ReclaimOwner(uint32_t owner) {
    InterruptGuard guard;
    if (!owner) return;
    for (uint32_t i = 0; i < mappingLimit; ++i) {
        Mapping& mapping = mappings[i];
        if (mapping.owner != owner) continue;
        DropReference(*mapping.object);
        mapping = {};
    }
    for (uint32_t i = 0; i < handleLimit; ++i) {
        Handle& handle = handles[i];
        if (handle.owner != owner) continue;
        DropReference(*handle.object);
        handle = {};
    }
}
