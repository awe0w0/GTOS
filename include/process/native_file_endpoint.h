#ifndef GTOS_PROCESS_NATIVE_FILE_ENDPOINT_H
#define GTOS_PROCESS_NATIVE_FILE_ENDPOINT_H
#include <memory/process_address_space.h>
namespace gtos { namespace process {
// Non-owning BSP endpoint. Must outlive the activated runtime.
// Keep the core runtime independent of filesystem-private C headers/objects.
class NativeFileEndpoint {
protected:
    ~NativeFileEndpoint() {}
public:
    virtual int32_t Call(uint32_t owner,memory::ProcessAddressSpace& space,
        uint32_t operation,uint32_t request,uint32_t bytes)=0;
    virtual void ReclaimOwner(uint32_t owner)=0;
};
} }
#endif
