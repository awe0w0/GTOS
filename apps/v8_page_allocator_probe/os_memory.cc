#include "probe.h"
#include "src/base/platform/platform.h"
#if !defined(__i386__) || !defined(__GTOS__) || defined(__linux__) || defined(__unix__) || defined(_WIN32)
#error This memory bridge requires the real GTOS i386 data-page ABI
#endif
#if !V8_OS_GTOS || !defined(V8_TARGET_OS_GTOS) || V8_OS_POSIX || V8_OS_LINUX
#error GTOS must retain its independent V8 platform identity
#endif
static_assert(sizeof(void*)==4,"GTOS IA32 pointer ABI");
namespace {
    struct Region { unsigned handle,base,length; };
    Region regions[16];
    unsigned mmap_seed=0x6d2b79f5U;
    Region* exact(void* address) {
        const unsigned base=(unsigned)address;
        for(unsigned i=0;i<16;++i)
            if(regions[i].handle && regions[i].base==base) return regions+i;
        return nullptr;
    }
    Region* containing(void* address,size_t length) {
        const unsigned base=(unsigned)address;
        if(!length || ((base|length)&4095U)) return nullptr;
        for(unsigned i=0;i<16;++i) {
            Region* r=regions+i;
            if(r->handle && base>=r->base && base-r->base<r->length
                && length<=r->length-(base-r->base)) return r;
        }
        return nullptr;
    }
    bool protection(v8::base::OS::MemoryPermission access,unsigned* result) {
        switch(access) {
        case v8::base::OS::MemoryPermission::kNoAccess:*result=GTOS_VM_NONE;return true;
        case v8::base::OS::MemoryPermission::kRead:*result=GTOS_VM_READ;return true;
        case v8::base::OS::MemoryPermission::kReadWrite:*result=GTOS_VM_READ_WRITE;return true;
        default:return false; // Non-PAE has no NX/JIT capability to advertise.
        }
    }
    bool range(unsigned call,void* address,size_t length,unsigned permissions) {
        Region* r=containing(address,length);
        if(!r) return false;
        GtosVmRangeRequest q={1,r->handle,(unsigned)address-r->base,(unsigned)length,permissions};
        return vm_probe_call(call,(unsigned)&q,sizeof(q))==0;
    }
    void release(Region* r) {
        GtosVmControlRequest q={1,r->handle,0};
        if(vm_probe_call(GTOS_SYS_VM_RELEASE,(unsigned)&q,sizeof(q))!=0) vm_probe_panic(0xa1);
        *r={};
    }
}
bool vm_probe_info(void* address,GtosVmRegionInfo* result) {
    Region* r=exact(address);
    if(!r || !result) return false;
    GtosVmQueryRequest q={1,r->handle,(unsigned)result};
    if(vm_probe_call(GTOS_SYS_VM_QUERY,(unsigned)&q,sizeof(q))!=0) return false;
    return result->version==1 && result->handle==r->handle && result->base==r->base
        && result->length==r->length;
}
unsigned vm_probe_handle(void* address) {
    Region* r=exact(address);return r?r->handle:0;
}
namespace v8 { namespace base {
size_t OS::AllocatePageSize() { return GTOS_VM_PAGE_BYTES; }
size_t OS::CommitPageSize() { return GTOS_VM_PAGE_BYTES; }
void OS::SetRandomMmapSeed(int64_t seed) {
    mmap_seed=(unsigned)seed^(unsigned)((uint64_t)seed>>32);
    if(!mmap_seed) mmap_seed=0x6d2b79f5U;
}
void* OS::GetRandomMmapAddr() {
    // An optional reproducible PRNG placement hint, never an entropy/ASLR promise.
    mmap_seed^=mmap_seed<<13;
    mmap_seed^=mmap_seed>>17;
    mmap_seed^=mmap_seed<<5;
    return (void*)(0x80000000U|((mmap_seed&0x1ffffU)<<12));
}
void* OS::Allocate(void* hint,size_t size,size_t alignment,MemoryPermission access,
                   std::optional<SharedMemoryHandle> handle) {
    unsigned permissions;
    if(handle.has_value() || !protection(access,&permissions) || !size || (size&4095U)
        || alignment<4096U || (alignment&(alignment-1U))) return nullptr;
    Region* r=nullptr;
    for(unsigned i=0;i<16;++i) if(!regions[i].handle) { r=regions+i;break; }
    if(!r) return nullptr;
    GtosVmReserveResult result={};
    GtosVmReserveRequest q={1,(unsigned)size,(unsigned)alignment,(unsigned)hint,(unsigned)&result};
    int status=vm_probe_call(GTOS_SYS_VM_RESERVE,(unsigned)&q,sizeof(q));
    if(status!=0 && hint) {
        q.hint=0;status=vm_probe_call(GTOS_SYS_VM_RESERVE,(unsigned)&q,sizeof(q));
    }
    if(status!=0) return nullptr;
    if(result.version!=1 || !result.handle || result.handle>0x7fffffffU
        || result.page_size!=4096U || result.length!=size || !result.base
        || (result.base&(alignment-1U))) vm_probe_panic(0xa2);
    *r={result.handle,result.base,result.length};
    void* address=(void*)r->base;
    if(permissions!=GTOS_VM_NONE && !range(GTOS_SYS_VM_SET_PERMISSIONS,address,size,permissions)) {
        release(r);return nullptr;
    }
    return address;
}
void OS::Free(void* address,size_t size) {
    Region* r=exact(address);
    if(!r || size!=r->length) vm_probe_panic(0xa3);
    release(r);
}
void OS::Release(void* address,size_t size) {
    Region* r=containing(address,size);
    if(!r || (unsigned)address-r->base+size!=r->length) vm_probe_panic(0xa4);
    const unsigned retained=(unsigned)address-r->base;
    if(!retained) { release(r);return; }
    GtosVmControlRequest q={1,r->handle,retained};
    if(vm_probe_call(GTOS_SYS_VM_TRIM,(unsigned)&q,sizeof(q))!=0) vm_probe_panic(0xa5);
    r->length=retained;
}
bool OS::SetPermissions(void* address,size_t size,MemoryPermission access) {
    unsigned permissions;
    return protection(access,&permissions) && range(GTOS_SYS_VM_SET_PERMISSIONS,address,size,permissions);
}
bool OS::RecommitPages(void* address,size_t size,MemoryPermission access) {
    // The upstream contract requires the original permissions after discard.
    // GTOS discard keeps backing and permissions. Perform a checked real
    // permissions operation, rather than returning fabricated success.
    return SetPermissions(address,size,access);
}
bool OS::DiscardSystemPages(void* address,size_t size) {
    return range(GTOS_SYS_VM_DISCARD,address,size,0);
}
bool OS::DecommitPages(void* address,size_t size) {
    return range(GTOS_SYS_VM_DECOMMIT,address,size,0);
}
bool OS::SealPages(void*,size_t) { return false; }
} }
