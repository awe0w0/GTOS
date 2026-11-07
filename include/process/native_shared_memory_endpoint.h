#ifndef GTOS_PROCESS_NATIVE_SHARED_MEMORY_ENDPOINT_H
#define GTOS_PROCESS_NATIVE_SHARED_MEMORY_ENDPOINT_H

#include <memory/process_address_space.h>

namespace gtos { namespace process {

// Synchronous BSP service. The runtime owns neither this endpoint nor its
// backing allocator. Both must outlive activation and every deferred reap.
class NativeSharedMemoryEndpoint {
protected:
    ~NativeSharedMemoryEndpoint() {}
public:
    virtual int32_t Call(uint32_t owner, memory::ProcessAddressSpace& space,
        uint32_t operation, uint32_t request, uint32_t bytes) = 0;
    // Only after the process address space has been destroyed.
    virtual void ReclaimOwner(uint32_t owner) = 0;
};

} }
#endif
