#include <process/native_events.h>
#include <memory/criticalsection.h>
using namespace gtos;
using namespace gtos::memory;
using namespace gtos::process;
NativeEvents::NativeEvents():nextId(1),statistics(),notify(0),notifyContext(0) {
    for (uint32_t i=0;i<MaximumHandles;++i) handles[i]=Handle();
    for (uint32_t i=0;i<MaximumObjects;++i) objects[i]=Object();
}
bool NativeEvents::SetNotifier(void (*callback)(void*),void* context) {
    InterruptGuard guard;
    if (notify||!callback||!context) return false;
    notify=callback;notifyContext=context;return true;
}
void NativeEvents::Notify() {if (notify) notify(notifyContext);}
int NativeEvents::Find(uint32_t owner,uint32_t id) const {
    if (owner&&id) for (uint32_t i=0;i<MaximumHandles;++i)
        if (handles[i].id==id&&handles[i].owner==owner) return (int)i;
    return -1;
}
int NativeEvents::FreeHandle() const {
    for (uint32_t i=0;i<MaximumHandles;++i) if (!handles[i].id) return (int)i;
    return -1;
}
int NativeEvents::PlanCreate(uint32_t owner,uint32_t flags,uint32_t rights,int& handle,int& object) const {
    if (!owner||(flags&~3U)||!rights||(rights&~GTOS_EVENT_RIGHT_ALL)) return GTOS_EVENT_ERR_INVALID;
    if (!nextId||nextId>0x7fffffffU) return GTOS_EVENT_ERR_OVERFLOW;
    handle=FreeHandle();object=-1;
    for (uint32_t i=0;i<MaximumObjects;++i) if (!objects[i].references) {object=(int)i;break;}
    return handle<0||object<0?GTOS_EVENT_ERR_LIMIT:0;
}
uint32_t NativeEvents::PublishCreate(uint32_t owner,uint32_t flags,uint32_t rights,int handle,int object) {
    Object& value=objects[object];value.references=1;
    value.manual=(flags&GTOS_EVENT_MANUAL_RESET)!=0;value.signalled=(flags&GTOS_EVENT_INITIAL_SIGNAL)!=0;
    Handle& alias=handles[handle];alias.id=nextId++;alias.owner=owner;alias.object=(uint32_t)object;alias.rights=rights;
    ++statistics.handles;++statistics.objects;++statistics.created;return alias.id;
}
int32_t NativeEvents::CreateForOwner(uint32_t owner,uint32_t flags,uint32_t rights) {
    InterruptGuard guard;int handle,object;int result=PlanCreate(owner,flags,rights,handle,object);
    return result?result:(int32_t)PublishCreate(owner,flags,rights,handle,object);
}
int32_t NativeEvents::DuplicateForOwner(uint32_t owner,uint32_t id,uint32_t recipient,uint32_t rights) {
    InterruptGuard guard;int index=Find(owner,id);
    if (index<0) return GTOS_EVENT_ERR_BAD_HANDLE;
    const Handle source=handles[index];
    if (!(source.rights&GTOS_EVENT_RIGHT_DUP)) return GTOS_EVENT_ERR_PERMISSION;
    if (!rights) rights=source.rights;
    if ((rights&~source.rights)||!recipient) return GTOS_EVENT_ERR_PERMISSION;
    if (!nextId||nextId>0x7fffffffU) return GTOS_EVENT_ERR_OVERFLOW;
    int target=FreeHandle();if (target<0) return GTOS_EVENT_ERR_LIMIT;
    Handle& alias=handles[target];alias.id=nextId++;alias.owner=recipient;alias.object=source.object;alias.rights=rights;
    ++objects[source.object].references;++statistics.handles;++statistics.duplicated;return (int32_t)alias.id;
}
void NativeEvents::Drop(uint32_t index) {
    Object& value=objects[handles[index].object];
    if (!--value.references) {value.manual=value.signalled=false;--statistics.objects;}
    handles[index]=Handle();--statistics.handles;++statistics.closed;
}
int32_t NativeEvents::Close(uint32_t owner,uint32_t id) {
    InterruptGuard guard;int index=Find(owner,id);if (index<0) return GTOS_EVENT_ERR_BAD_HANDLE;
    Drop((uint32_t)index);Notify();return 0;
}
int32_t NativeEvents::Set(uint32_t owner,uint32_t id) {
    InterruptGuard guard;int index=Find(owner,id);if (index<0) return GTOS_EVENT_ERR_BAD_HANDLE;
    if (!(handles[index].rights&GTOS_EVENT_RIGHT_SET)) return GTOS_EVENT_ERR_PERMISSION;
    objects[handles[index].object].signalled=true;++statistics.signals;Notify();return 0;
}
int32_t NativeEvents::Reset(uint32_t owner,uint32_t id) {
    InterruptGuard guard;int index=Find(owner,id);if (index<0) return GTOS_EVENT_ERR_BAD_HANDLE;
    if (!(handles[index].rights&GTOS_EVENT_RIGHT_RESET)) return GTOS_EVENT_ERR_PERMISSION;
    objects[handles[index].object].signalled=false;return 0;
}
int32_t NativeEvents::Select(uint32_t owner,const uint32_t* values,uint32_t count) {
    InterruptGuard guard;
    if (!values||!count||count>GTOS_EVENT_WAIT_LIMIT) return GTOS_EVENT_ERR_INVALID;
    int indices[GTOS_EVENT_WAIT_LIMIT];
    // Complete validation before any automatic event can lose its signal.
    for (uint32_t i=0;i<count;++i) {
        indices[i]=Find(owner,values[i]);if (indices[i]<0) return GTOS_EVENT_ERR_BAD_HANDLE;
        if (!(handles[indices[i]].rights&GTOS_EVENT_RIGHT_WAIT)) return GTOS_EVENT_ERR_PERMISSION;
    }
    for (uint32_t i=0;i<count;++i) {
        Object& value=objects[handles[indices[i]].object];
        if (!value.signalled) continue;
        if (!value.manual) value.signalled=false;return (int32_t)i;
    }
    return GTOS_EVENT_ERR_PENDING;
}
void NativeEvents::ReclaimOwner(uint32_t owner) {
    InterruptGuard guard;if (!owner) return;
    ++statistics.reclaims;
    for (uint32_t i=0;i<MaximumHandles;++i) if (handles[i].id&&handles[i].owner==owner) {Drop(i);++statistics.released;}
    Notify();
}
NativeEventStatistics NativeEvents::Statistics() const {InterruptGuard guard;return statistics;}
int32_t NativeEvents::Call(uint32_t owner,ProcessAddressSpace& space,uint32_t operation,uint32_t address,uint32_t bytes) {
    InterruptGuard guard;
    if (operation==GTOS_SYS_EVENT_CREATE) {
        if (bytes!=sizeof(GtosEventCreateRequest)) return GTOS_EVENT_ERR_INVALID;
        GtosEventCreateRequest request;
        if (!space.CopyFromUser(&request,address,sizeof(request))) return GTOS_EVENT_ERR_BAD_ADDRESS;
        if (request.version!=GTOS_EVENT_ABI_VERSION) return GTOS_EVENT_ERR_UNAVAILABLE;
        if (!space.ValidateUserRange(request.result_va,sizeof(uint32_t),true)) return GTOS_EVENT_ERR_BAD_ADDRESS;
        int handle,object;int result=PlanCreate(owner,request.flags,GTOS_EVENT_RIGHT_ALL,handle,object);
        if (result) return result;
        // IF=0 on BSP: publication cannot fail after this fully checked copy.
        if (!space.CopyToUser(request.result_va,&nextId,sizeof(nextId))) return GTOS_EVENT_ERR_BAD_ADDRESS;
        PublishCreate(owner,request.flags,GTOS_EVENT_RIGHT_ALL,handle,object);return 0;
    }
    if (operation==GTOS_SYS_EVENT_DUP) {
        if (bytes!=sizeof(GtosEventDupRequest)) return GTOS_EVENT_ERR_INVALID;
        GtosEventDupRequest request;
        if (!space.CopyFromUser(&request,address,sizeof(request))) return GTOS_EVENT_ERR_BAD_ADDRESS;
        if (request.version!=GTOS_EVENT_ABI_VERSION) return GTOS_EVENT_ERR_UNAVAILABLE;
        return DuplicateForOwner(owner,request.handle,owner,request.rights);
    }
    if (operation!=GTOS_SYS_EVENT_CLOSE&&operation!=GTOS_SYS_EVENT_SET&&operation!=GTOS_SYS_EVENT_RESET)
        return GTOS_EVENT_ERR_UNAVAILABLE;
    if (bytes!=sizeof(GtosEventControlRequest)) return GTOS_EVENT_ERR_INVALID;
    GtosEventControlRequest request;
    if (!space.CopyFromUser(&request,address,sizeof(request))) return GTOS_EVENT_ERR_BAD_ADDRESS;
    if (request.version!=GTOS_EVENT_ABI_VERSION) return GTOS_EVENT_ERR_UNAVAILABLE;
    if (operation==GTOS_SYS_EVENT_CLOSE) return Close(owner,request.handle);
    return operation==GTOS_SYS_EVENT_SET?Set(owner,request.handle):Reset(owner,request.handle);
}
