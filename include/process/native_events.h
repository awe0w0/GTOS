#ifndef GTOS_PROCESS_NATIVE_EVENTS_H
#define GTOS_PROCESS_NATIVE_EVENTS_H
#include <process/native_event_endpoint.h>
namespace gtos { struct NativeEventFixture; namespace process {
struct NativeEventStatistics {
    uint32_t handles,objects,created,duplicated,closed,signals,reclaims,released;
};
// Bounded BSP event service. Never allocates user/physical pages or heap memory.
class NativeEvents:public NativeEventEndpoint {
    friend struct ::gtos::NativeEventFixture;
public:
    static const uint32_t MaximumHandles=64,MaximumObjects=32;
private:
    struct Object { uint32_t references;bool manual,signalled; } objects[MaximumObjects];
    struct Handle { uint32_t id,owner,object,rights; } handles[MaximumHandles];
    uint32_t nextId;
    NativeEventStatistics statistics;
    void (*notify)(void*);
    void* notifyContext;
    int Find(uint32_t owner,uint32_t id) const;
    int FreeHandle() const;
    int PlanCreate(uint32_t owner,uint32_t flags,uint32_t rights,int& handle,int& object) const;
    uint32_t PublishCreate(uint32_t owner,uint32_t flags,uint32_t rights,int handle,int object);
    void Drop(uint32_t index);
    void Notify();
    NativeEvents(const NativeEvents&);
    NativeEvents& operator=(const NativeEvents&);
public:
    NativeEvents();
    virtual bool SetNotifier(void (*callback)(void*),void* context);
    virtual int32_t Call(uint32_t owner,memory::ProcessAddressSpace& space,
        uint32_t operation,uint32_t request,uint32_t bytes);
    virtual int32_t Select(uint32_t owner,const uint32_t* values,uint32_t count);
    virtual void ReclaimOwner(uint32_t owner);
    // Trusted kernel admission only; not exposed as owner/PID syscall fields.
    int32_t CreateForOwner(uint32_t owner,uint32_t flags,uint32_t rights=GTOS_EVENT_RIGHT_ALL);
    int32_t DuplicateForOwner(uint32_t owner,uint32_t id,uint32_t recipient,uint32_t rights);
    int32_t Close(uint32_t owner,uint32_t id);
    int32_t Set(uint32_t owner,uint32_t id);
    int32_t Reset(uint32_t owner,uint32_t id);
    NativeEventStatistics Statistics() const;
};
} }
#endif
