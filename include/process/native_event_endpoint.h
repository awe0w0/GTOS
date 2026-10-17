#ifndef GTOS_PROCESS_NATIVE_EVENT_ENDPOINT_H
#define GTOS_PROCESS_NATIVE_EVENT_ENDPOINT_H
#include <memory/process_address_space.h>
#include <process/event_abi.h>
namespace gtos { namespace process {
// Non-owning BSP endpoint, alive for the runtime lifetime. All callbacks run
// synchronously with IF clear. The callback may Select, but must not reenter
// Call/Create/Duplicate/Close/Set/Reset/ReclaimOwner or replace the notifier.
class NativeEventEndpoint {
protected:
    ~NativeEventEndpoint() {}
public:
    virtual bool SetNotifier(void (*notify)(void*),void* context)=0;
    virtual int32_t Call(uint32_t owner,memory::ProcessAddressSpace& space,
        uint32_t operation,uint32_t request,uint32_t bytes)=0;
    // Kernel snapshot only, no user pointers. PENDING means no ready event.
    virtual int32_t Select(uint32_t owner,const uint32_t* handles,uint32_t count)=0;
    virtual void ReclaimOwner(uint32_t owner)=0;
};
} }
#endif
