#ifndef GTOS_PROCESS_NATIVE_TCP_ENDPOINT_H
#define GTOS_PROCESS_NATIVE_TCP_ENDPOINT_H

#include <memory/process_address_space.h>
#include <process/tcp_abi.h>

namespace gtos { namespace process {

class NativeTcpEndpoint {
protected:
    ~NativeTcpEndpoint() {}
public:
    virtual int32_t Call(uint32_t owner, memory::ProcessAddressSpace& space,
        uint32_t operation, uint32_t address, uint32_t bytes) = 0;
    virtual void ReclaimOwner(uint32_t owner) = 0;
};

} }

#endif
