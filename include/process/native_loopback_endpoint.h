#ifndef GTOS_PROCESS_NATIVE_LOOPBACK_ENDPOINT_H
#define GTOS_PROCESS_NATIVE_LOOPBACK_ENDPOINT_H

#include <memory/process_address_space.h>
#include <process/loopback_abi.h>

namespace gtos { namespace process {

class NativeLoopbackEndpoint {
protected:
    ~NativeLoopbackEndpoint() {}
public:
    virtual int32_t Call(uint32_t owner, memory::ProcessAddressSpace& space,
        uint32_t operation, uint32_t address, uint32_t bytes) = 0;
    virtual void ReclaimOwner(uint32_t owner) = 0;
};

} }

#endif
