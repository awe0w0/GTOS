#ifndef GTOS_PROCESS_NATIVE_SHARED_MEMORY_H
#define GTOS_PROCESS_NATIVE_SHARED_MEMORY_H

#include <process/native_shared_memory_endpoint.h>

namespace gtos { namespace process {

class NativeSharedMemory : public NativeSharedMemoryEndpoint {
public:
    static const uint32_t MaximumObjects = 32, MaximumHandles = 64, MaximumMappings = 64;
    static const uint32_t ContentMaximumObjects = 256, ContentMaximumHandles = 512,
        ContentMaximumMappings = 512;
    explicit NativeSharedMemory(memory::PhysicalMemoryManager& allocator);
    bool EnableContentProfile();
    int32_t Call(uint32_t owner, memory::ProcessAddressSpace& space,
        uint32_t operation, uint32_t request, uint32_t bytes) override;
    void ReclaimOwner(uint32_t owner) override;

private:
    struct Object {
        uint32_t firstFrame, bytes, mode, references;
    };
    struct Handle {
        uint32_t id, owner;
        Object* object;
    };
    struct Mapping {
        uint32_t owner, base, bytes, region;
        Object* object;
    };
    memory::PhysicalMemoryManager& frames;
    Object objects[ContentMaximumObjects];
    Handle handles[ContentMaximumHandles];
    Mapping mappings[ContentMaximumMappings];
    uint32_t nextHandle;
    uint32_t objectLimit, handleLimit, mappingLimit;

    NativeSharedMemory(const NativeSharedMemory&) = delete;
    NativeSharedMemory& operator=(const NativeSharedMemory&) = delete;
    Handle* FindHandle(uint32_t owner, uint32_t id);
    void DropReference(Object& object);
    int32_t Create(uint32_t owner, memory::ProcessAddressSpace& space,
        uint32_t bytes, uint32_t mode);
    int32_t Duplicate(uint32_t owner, Object& object);
    int32_t Map(uint32_t owner, memory::ProcessAddressSpace& space,
        uint32_t handle, uint32_t offset, uint32_t bytes, uint32_t writable, uint32_t result);
    int32_t Unmap(uint32_t owner, memory::ProcessAddressSpace& space,
        uint32_t base, uint32_t bytes);
};

} }
#endif
