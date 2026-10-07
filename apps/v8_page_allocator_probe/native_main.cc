#include "probe.h"
#include <process/abi.h>
#include "src/base/page-allocator.h"
#include <string.h>
#ifndef GTOS_VM_FAULT_MODE
#define GTOS_VM_FAULT_MODE 0
#endif
static_assert(GTOS_VM_FAULT_MODE>=0 && GTOS_VM_FAULT_MODE<=6,"declared VM guest mode");
extern "C" {
__attribute__((section(".data.vm_record"),used)) volatile VmProbeRecord vm_probe_record={1,GTOS_VM_FAULT_MODE,0,0,0,0};
}
extern "C" int vm_probe_registers(unsigned,unsigned,unsigned);
extern "C" unsigned char vm_probe_rw_limit;
alignas(4096) static unsigned char boundary[8192];
static unsigned region_handles[32];
[[maybe_unused]] static volatile unsigned initialized=0x47545638U;
#define CHECK(c,id) do { if(!(c)) return 0x56000000U|(id); } while(false)
static int invoke(unsigned call,void* request,unsigned bytes) {
    return vm_probe_call(call,(unsigned)request,bytes);
}
static bool untouched(const unsigned char* p,unsigned n,unsigned v) {
    for(unsigned i=0;i<n;++i) if(p[i]!=v) return false;
    return true;
}
[[maybe_unused]] static int negative_wire() {
    GtosVmReserveResult result;
    memset(&result,0xa5,sizeof(result));
    GtosVmReserveRequest q={1,4096,4096,0,(unsigned)&result};
    CHECK(invoke(GTOS_SYS_VM_RESERVE,&q,19)==GTOS_VM_ERR_BAD_SIZE,1);
    CHECK(invoke(GTOS_SYS_VM_RESERVE,&q,21)==GTOS_VM_ERR_BAD_SIZE,2);
    CHECK(vm_probe_call(GTOS_SYS_VM_RESERVE,0,20)==GTOS_VM_ERR_BAD_ADDRESS,3);
    CHECK(vm_probe_call(GTOS_SYS_VM_RESERVE,0x100000U,20)==GTOS_VM_ERR_BAD_ADDRESS,4);
    CHECK(vm_probe_call(GTOS_SYS_VM_RESERVE,0xfffffff8U,20)==GTOS_VM_ERR_BAD_ADDRESS,5);
    CHECK(vm_probe_call(GTOS_SYS_VM_RESERVE,(unsigned)&vm_probe_rw_limit-8,20)==GTOS_VM_ERR_BAD_ADDRESS,6);
    q.version=0;CHECK(invoke(GTOS_SYS_VM_RESERVE,&q,20)==GTOS_VM_ERR_UNSUPPORTED_VERSION,7);
    q.version=1;q.length=0;CHECK(invoke(GTOS_SYS_VM_RESERVE,&q,20)==GTOS_VM_ERR_RANGE,8);
    q.length=4095;CHECK(invoke(GTOS_SYS_VM_RESERVE,&q,20)==GTOS_VM_ERR_RANGE,9);
    q.length=0xfffff000U;CHECK(invoke(GTOS_SYS_VM_RESERVE,&q,20)==GTOS_VM_ERR_RANGE,10);
    q.length=4096;q.alignment=8193;CHECK(invoke(GTOS_SYS_VM_RESERVE,&q,20)==GTOS_VM_ERR_RANGE,11);
    q.alignment=4096;q.hint=0x100000U;CHECK(invoke(GTOS_SYS_VM_RESERVE,&q,20)==GTOS_VM_ERR_RANGE,12);
    q.hint=0xbfffd000U;CHECK(invoke(GTOS_SYS_VM_RESERVE,&q,20)==GTOS_VM_ERR_RANGE,13);
    q.hint=0x90000001U;CHECK(invoke(GTOS_SYS_VM_RESERVE,&q,20)==GTOS_VM_ERR_RANGE,14);
    q.hint=0;q.result=0;CHECK(invoke(GTOS_SYS_VM_RESERVE,&q,20)==GTOS_VM_ERR_BAD_ADDRESS,15);
    static const GtosVmReserveResult readonly_result={};
    q.result=(unsigned)&readonly_result;
    CHECK(invoke(GTOS_SYS_VM_RESERVE,&q,20)==GTOS_VM_ERR_BAD_ADDRESS,16);
    q.result=(unsigned)&vm_probe_rw_limit-8;
    CHECK(invoke(GTOS_SYS_VM_RESERVE,&q,20)==GTOS_VM_ERR_BAD_ADDRESS,17);
    CHECK(untouched((unsigned char*)&result,sizeof(result),0xa5),18);
    static const GtosVmRangeRequest readonly_range={1,0,0,4096,3};
    CHECK(invoke(GTOS_SYS_VM_SET_PERMISSIONS,(void*)&readonly_range,20)==GTOS_VM_ERR_BAD_STATE,19);
    // Both a complete request and its complete output can cross mapped pages.
    GtosVmReserveRequest* crossing=(GtosVmReserveRequest*)(boundary+4096-8);
    *crossing={1,4096,4096,0,(unsigned)(boundary+4096-4)};
    CHECK(vm_probe_registers(GTOS_SYS_VM_RESERVE,(unsigned)crossing,20)==1,20);
    memcpy(&result,boundary+4096-4,sizeof(result));
    CHECK(result.version==1 && result.handle && result.base>=0x80000000U
          && result.length==4096 && result.page_size==4096,21);
    GtosVmControlRequest control={1,result.handle,0};
    CHECK(vm_probe_registers(GTOS_SYS_VM_RELEASE,(unsigned)&control,12)==1,22);
    // Output overlap must use the snapshotted input, then one complete copy.
    GtosVmReserveRequest overlap={1,4096,4096,0,0};
    overlap.result=(unsigned)&overlap;
    CHECK(invoke(GTOS_SYS_VM_RESERVE,&overlap,20)==0,23);
    memcpy(&result,&overlap,sizeof(result));control.handle=result.handle;
    CHECK(result.version==1 && result.length==4096 && result.base>=0x80000000U,24);
    CHECK(invoke(GTOS_SYS_VM_RELEASE,&control,12)==0,25);
    CHECK(invoke(GTOS_SYS_VM_RELEASE,&control,12)==GTOS_VM_ERR_BAD_STATE,26);
    GtosVmQueryRequest query={1,control.handle,(unsigned)&result};
    CHECK(invoke(GTOS_SYS_VM_QUERY,&query,12)==GTOS_VM_ERR_BAD_STATE,27);
    // Region slots are bounded independently from physical resident pages.
    for(unsigned i=0;i<32;++i) {
        q={1,4096,4096,0,(unsigned)&result};
        CHECK(invoke(GTOS_SYS_VM_RESERVE,&q,20)==0,28);
        region_handles[i]=result.handle;
        query={1,result.handle,(unsigned)&result};
        GtosVmRegionInfo info={};query.result=(unsigned)&info;
        CHECK(invoke(GTOS_SYS_VM_QUERY,&query,12)==0 && info.resident_pages==0,29);
    }
    CHECK(invoke(GTOS_SYS_VM_RESERVE,&q,20)==GTOS_VM_ERR_LIMIT,30);
    for(unsigned i=0;i<32;++i) {
        control={1,region_handles[i],0};
        CHECK(invoke(GTOS_SYS_VM_RELEASE,&control,12)==0,31);
    }
    return 0;
}
static bool zero_page(const volatile unsigned char* p) {
    for(unsigned i=0;i<4096;++i) if(p[i]) return false;
    return true;
}
[[maybe_unused]] static bool pattern_page(const volatile unsigned char* p,unsigned salt) {
    for(unsigned i=0;i<4096;++i) if(p[i]!=(unsigned char)(i^salt)) return false;
    return true;
}
static void pattern(volatile unsigned char* p,unsigned salt) {
    for(unsigned i=0;i<4096;++i) p[i]=(unsigned char)(i^salt);
}
[[maybe_unused]] static void armed(unsigned mode,unsigned address) {
    char receipt[]="GTOS V8 PAGE ALLOCATOR FAULT ARMED V1 mode=0 address=00000000\n";
    static const char digits[]="0123456789abcdef";
    receipt[43]=(char)('0'+mode);
    for(unsigned i=0;i<8;++i) receipt[53+i]=digits[(address>>(28-i*4))&15];
    vm_probe_call(GTOS_SYS_WRITE,(unsigned)receipt,sizeof(receipt)-1);
}
extern "C" int vm_guest_main(void) {
    static const char start[]="GTOS V8 PAGE ALLOCATOR START V1\n";
    [[maybe_unused]] static const char pass[]="GTOS V8 PAGE ALLOCATOR PASS DATA VM V1\n";
    CHECK(vm_probe_call(GTOS_SYS_ABI,0,0)==1,40);
    CHECK(vm_probe_call(GTOS_SYS_WRITE,(unsigned)start,sizeof(start)-1)==(int)sizeof(start)-1,41);
    CHECK(vm_probe_call(0xffffffffU,0,0)==GTOS_ERR_UNSUPPORTED,42);
#if GTOS_VM_FAULT_MODE==0
    int rejected=negative_wire();if(rejected) return rejected;
#endif
    v8::base::PageAllocator concrete;
    v8::PageAllocator* pages=&concrete;
    CHECK(pages->AllocatePageSize()==4096 && pages->CommitPageSize()==4096,43);
    CHECK(!pages->CanAllocateSharedPages(),44);
    pages->SetRandomMmapSeed(0x12345678);
    const unsigned first_hint=(unsigned)pages->GetRandomMmapAddr();
    const unsigned next_hint=(unsigned)pages->GetRandomMmapAddr();
    pages->SetRandomMmapSeed(0x12345678);
    CHECK((unsigned)pages->GetRandomMmapAddr()==first_hint
        && (unsigned)pages->GetRandomMmapAddr()==next_hint
        && first_hint!=next_hint && !((first_hint|next_hint)&4095U),45);
    pages->SetRandomMmapSeed(0);
    const unsigned zero_seed=(unsigned)pages->GetRandomMmapAddr();
    CHECK((unsigned)pages->GetRandomMmapAddr()!=zero_seed,100);
    pages->SetRandomMmapSeed(0);
    CHECK((unsigned)pages->GetRandomMmapAddr()==zero_seed,101);
    // The virtual interval exceeds the entire current physical process quota.
    const unsigned size=16U*1024U*1024U;
    unsigned char* base=(unsigned char*)pages->AllocatePages((void*)0x90000000U,size,65536,v8::PageAllocator::kNoAccess);
    CHECK(base==(unsigned char*)0x90000000U,46);
    GtosVmRegionInfo info={};
    CHECK(vm_probe_info(base,&info) && info.resident_pages==0 && info.length==size,47);
    CHECK(pages->SetPermissions(base,3*4096,v8::PageAllocator::kReadWrite),48);
    CHECK(vm_probe_info(base,&info) && info.resident_pages==3,49);
    CHECK(zero_page(base) && zero_page(base+4096) && zero_page(base+8192),50);
    pattern(base,0x31);pattern(base+4096,0x72);pattern(base+8192,0xc4);
#if GTOS_VM_FAULT_MODE!=0
    unsigned char* victim=base;
#if GTOS_VM_FAULT_MODE==1
    CHECK(pages->SetPermissions(victim,4096,v8::PageAllocator::kRead),51);
#elif GTOS_VM_FAULT_MODE==2
    CHECK(pages->SetPermissions(victim,4096,v8::PageAllocator::kNoAccess),52);
#elif GTOS_VM_FAULT_MODE==3
    CHECK(pages->DecommitPages(victim,4096),53);
#elif GTOS_VM_FAULT_MODE==4
    CHECK(pages->FreePages(base,size),54);
#elif GTOS_VM_FAULT_MODE==5
    CHECK(pages->ReleasePages(base,size,2*4096),55);
    victim=base+8192;
#endif
    vm_probe_record.base=(unsigned)base;
    vm_probe_record.handle=info.handle;
    vm_probe_record.stage=1;
    armed(GTOS_VM_FAULT_MODE,(unsigned)victim);
#if GTOS_VM_FAULT_MODE==6
    for(;;) vm_probe_call(GTOS_SYS_YIELD,0,0);
#elif GTOS_VM_FAULT_MODE<=2
    *(volatile unsigned char*)victim=0x55;
#else
    volatile unsigned observed=*(volatile unsigned char*)victim;
    (void)observed;
#endif
    return 0x560000f0U; // A missing real fault can never become a pass.
#else
    GtosVmRangeRequest range={1,info.handle,0,256U*4096U,3};
    CHECK(invoke(GTOS_SYS_VM_SET_PERMISSIONS,&range,20)==GTOS_VM_ERR_LIMIT,60);
    CHECK(vm_probe_info(base,&info) && info.resident_pages==3 && pattern_page(base+4096,0x72),61);
    range={1,info.handle,0,4096,7};
    CHECK(invoke(GTOS_SYS_VM_SET_PERMISSIONS,&range,20)==GTOS_VM_ERR_PERMISSION,62);
    range.protection=3;range.offset=0xfffff000U;
    CHECK(invoke(GTOS_SYS_VM_SET_PERMISSIONS,&range,20)==GTOS_VM_ERR_RANGE,63);
    range.offset=0;range.length=4095;
    CHECK(invoke(GTOS_SYS_VM_SET_PERMISSIONS,&range,20)==GTOS_VM_ERR_RANGE,64);
    range.length=4096;range.version=0;
    CHECK(invoke(GTOS_SYS_VM_SET_PERMISSIONS,&range,20)==GTOS_VM_ERR_UNSUPPORTED_VERSION,65);
    CHECK(!pages->AllocatePages(nullptr,4096,4096,v8::PageAllocator::kReadWriteExecute),66);
    CHECK(!pages->SetPermissions(base,4096,v8::PageAllocator::kReadExecute),67);
    CHECK(!pages->SealPages(base,4096),68);
    CHECK(pages->SetPermissions(base+4096,4096,v8::PageAllocator::kRead),69);
    CHECK(pattern_page(base+4096,0x72),70);
    CHECK(pages->SetPermissions(base+4096,4096,v8::PageAllocator::kReadWrite),71);
    CHECK(pattern_page(base+4096,0x72),72);
    CHECK(pages->SetPermissions(base+4096,4096,v8::PageAllocator::kNoAccess),73);
    CHECK(vm_probe_info(base,&info) && info.resident_pages==3,74);
    CHECK(pages->SetPermissions(base+4096,4096,v8::PageAllocator::kReadWrite),75);
    CHECK(pattern_page(base+4096,0x72),76);
    CHECK(pages->DecommitPages(base+4096,4096),77);
    CHECK(vm_probe_info(base,&info) && info.resident_pages==2,78);
    CHECK(pages->SetPermissions(base+4096,4096,v8::PageAllocator::kReadWrite),79);
    CHECK(zero_page(base+4096) && pattern_page(base,0x31) && pattern_page(base+8192,0xc4),80);
    pattern(base+4096,0x51);
    CHECK(pages->DiscardSystemPages(base+4096,4096),81);
    CHECK(pages->RecommitPages(base+4096,4096,v8::PageAllocator::kReadWrite),82);
    CHECK(zero_page(base+4096) && pattern_page(base,0x31) && pattern_page(base+8192,0xc4),83);
    CHECK(vm_probe_info(base,&info) && info.resident_pages==3,84);
    // A used exact hint can fall back to a distinct aligned reservation.
    void* second=pages->AllocatePages(4096,4096,v8::PageAllocator::kReadWrite,
        v8::PageAllocator::AllocationHint().WithAddress(base));
    CHECK(second && second!=base && zero_page((unsigned char*)second),85);
    CHECK(pages->FreePages(second,4096),86);
    const unsigned old_handle=vm_probe_handle(base);
    CHECK(pages->ReleasePages(base,size,2*4096),87);
    CHECK(vm_probe_info(base,&info) && info.length==8192 && info.resident_pages==2,88);
    CHECK(pattern_page(base,0x31),89);
    CHECK(pages->FreePages(base,8192),90);
    GtosVmQueryRequest stale={1,old_handle,(unsigned)&info};
    CHECK(invoke(GTOS_SYS_VM_QUERY,&stale,12)==GTOS_VM_ERR_BAD_STATE,91);
    // Same address, new monotonic identity, fully zero fresh backing.
    base=(unsigned char*)pages->AllocatePages((void*)0x90000000U,4096,4096,v8::PageAllocator::kReadWrite);
    CHECK(base==(unsigned char*)0x90000000U && vm_probe_handle(base)>old_handle && zero_page(base),92);
    CHECK(pages->FreePages(base,4096),93);
    CHECK(initialized==0x47545638U,94);
    // Retain real resident VM so EXIT/fault/RequestExit and reap are observed.
    base=(unsigned char*)pages->AllocatePages(nullptr,16*4096,65536,v8::PageAllocator::kNoAccess);
    CHECK(base && pages->SetPermissions(base,3*4096,v8::PageAllocator::kReadWrite),95);
    CHECK(zero_page(base) && zero_page(base+4096) && zero_page(base+8192),96);
    pattern(base,0x99);pattern(base+4096,0x5a);pattern(base+8192,0xa6);
    CHECK(vm_probe_info(base,&info) && info.resident_pages==3,98);
    CHECK(pattern_page(base,0x99) && pattern_page(base+4096,0x5a) && pattern_page(base+8192,0xa6),99);
    vm_probe_record.base=(unsigned)base;
    vm_probe_record.handle=info.handle;
    vm_probe_record.stage=2;
    CHECK(vm_probe_call(GTOS_SYS_WRITE,(unsigned)pass,sizeof(pass)-1)==(int)sizeof(pass)-1,97);
    return 0;
#endif
}
