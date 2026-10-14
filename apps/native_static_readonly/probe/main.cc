#include <process/abi.h>
#include <process/vm_abi.h>
#include <process/thread_abi.h>
#include <process/readonly_abi.h>
#include "src/base/platform/platform.h"
#include "src/flags/flags.h"
#include "record.h"
extern "C" {
volatile ReadonlyRecord native_readonly_record __attribute__((section(".data.readonly_record"))) = {1};
alignas(4096) unsigned char seal_pages[8192] __attribute__((section(".data.readonly_pages"))) = {};
extern void (*__init_array_start[])();
extern void (*__init_array_end[])();
}
namespace {
using v8::base::OS;
using v8::internal::FlagList;
using v8::internal::v8_flags;
static_assert(alignof(v8::internal::FlagValues)==4096 && sizeof(v8_flags)%4096==0,"actual IA32 flags pages");
int Call(unsigned op,unsigned address=0,unsigned bytes=0) {
    asm volatile("int $0x80" : "+a"(op) : "b"(address), "c"(bytes) : "memory", "cc"); return (int)op;
}
template <class T> int Request(unsigned op,T& r) { return Call(op,(unsigned)&r,sizeof(r)); }
void Yield() { Call(GTOS_SYS_YIELD); }
void Need(bool pass,unsigned line) {
    asm volatile("lock incl %0" : "+m"(native_readonly_record.checks) : : "memory", "cc");
    if (!pass) { native_readonly_record.error=line; Call(GTOS_SYS_EXIT,line); for (;;) Yield(); }
}
#define NEED(x) Need((x),__LINE__)
unsigned Hash(const volatile unsigned char* data,unsigned bytes) {
    unsigned value=2166136261U;
    for (unsigned i=0;i<bytes;++i) value=(value^data[i])*16777619U;
    return value;
}
void Seal(unsigned address,unsigned bytes,int expected) {
    GtosDataReadonlyRequest request={1,address,bytes};
    NEED(Request(GTOS_SYS_DATA_READONLY,request)==expected);
}
void Negatives() {
    GtosDataReadonlyRequest request={1,(unsigned)seal_pages,sizeof(seal_pages)};
    for (unsigned bytes : {0U,11U,13U})
        NEED(Call(GTOS_SYS_DATA_READONLY,(unsigned)&request,bytes)==GTOS_READONLY_ERR_BAD_SIZE);
    NEED(Call(GTOS_SYS_DATA_READONLY,0,sizeof(request))==GTOS_READONLY_ERR_BAD_ADDRESS);
    NEED(Call(GTOS_SYS_DATA_READONLY,(unsigned)seal_pages+sizeof(seal_pages)-8,sizeof(request))==GTOS_READONLY_ERR_BAD_ADDRESS);
    request.version=2; NEED(Request(GTOS_SYS_DATA_READONLY,request)==GTOS_READONLY_ERR_UNSUPPORTED_VERSION);
    Seal((unsigned)seal_pages+1,4096,GTOS_READONLY_ERR_RANGE);
    Seal((unsigned)seal_pages,4095,GTOS_READONLY_ERR_RANGE);
    Seal((unsigned)seal_pages,0,GTOS_READONLY_ERR_RANGE);
    Seal(0,4096,GTOS_READONLY_ERR_RANGE);
    Seal(0xfffff000U,4096,GTOS_READONLY_ERR_RANGE);
    Seal((unsigned)seal_pages,0xfffff000U,GTOS_READONLY_ERR_RANGE);
    Seal(0xd0000000U,4096,GTOS_READONLY_ERR_RANGE);
    for (unsigned address : {0x40000000U,0x70000000U,0xbfffc000U,0xbfffd000U})
        Seal(address,4096,GTOS_READONLY_ERR_PERMISSION);
    // The third page is an unmapped gap. The first two must remain writable.
    Seal((unsigned)seal_pages,12288,GTOS_READONLY_ERR_PERMISSION);
    volatile unsigned char* data=seal_pages; data[0]=17; data[4096]=29;
    NEED(data[0]==17 && data[4096]==29);
    GtosVmReserveResult region={};
    GtosVmReserveRequest reserve={1,4096,4096,0,(unsigned)&region};
    NEED(Request(GTOS_SYS_VM_RESERVE,reserve)==0 && region.length==4096);
    GtosVmRangeRequest commit={1,region.handle,0,4096,GTOS_VM_READ_WRITE};
    NEED(Request(GTOS_SYS_VM_SET_PERMISSIONS,commit)==0);
    Seal(region.base,4096,GTOS_READONLY_ERR_PERMISSION);
    volatile unsigned* word=(volatile unsigned*)region.base; *word=0x31415926;
    NEED(*word==0x31415926);
    GtosVmControlRequest release={1,region.handle,0}; NEED(Request(GTOS_SYS_VM_RELEASE,release)==0);
}
void Worker(unsigned) {
    volatile unsigned char* flags=(volatile unsigned char*)&v8_flags;
    const unsigned char original=*flags; *flags=original; // Warm a writable TLB entry.
    native_readonly_record.worker_stage=1;
    while (native_readonly_record.go!=1) Yield();
    OS::SetDataReadOnly(&v8_flags,sizeof(v8_flags));
    NEED(FlagList::IsFrozen() && Hash(flags,sizeof(v8_flags))==native_readonly_record.before);
    native_readonly_record.worker_stage=2;
    while (native_readonly_record.go!=2) Yield();
    native_readonly_record.worker_stage=3;
    *flags=original^1U; NEED(false);
    Call(GTOS_SYS_THREAD_EXIT,0); for (;;) Yield();
}
void StartWorker() {
    GtosVmReserveResult region={};
    GtosVmReserveRequest reserve={1,4*4096,4096,0,(unsigned)&region};
    NEED(Request(GTOS_SYS_VM_RESERVE,reserve)==0);
    GtosVmRangeRequest stack={1,region.handle,4096,2*4096,GTOS_VM_READ_WRITE};
    NEED(Request(GTOS_SYS_VM_SET_PERMISSIONS,stack)==0);
    GtosThreadCreateRequest create={1,0,(unsigned)&Worker,0,region.handle,(unsigned)&native_readonly_record.worker};
    NEED(Request(GTOS_SYS_THREAD_CREATE,create)==0 && native_readonly_record.worker);
    while (native_readonly_record.worker_stage!=1) Yield();
}
void Main() {
    NEED(native_readonly_record.version==1 && native_readonly_record.mode<=4);
    Negatives();
    const unsigned mode=native_readonly_record.mode;
    volatile unsigned char* flags=(volatile unsigned char*)&v8_flags;
    native_readonly_record.flags_address=(unsigned)&v8_flags;
    native_readonly_record.flags_bytes=sizeof(v8_flags);
    native_readonly_record.before=Hash(flags,sizeof(v8_flags));
    NEED(!FlagList::IsFrozen());
    if (mode==3) StartWorker();
    const unsigned char original=*flags; *flags=original;
    FlagList::FreezeFlags(); NEED(FlagList::IsFrozen());
    FlagList::FreezeFlags();
    OS::SetDataReadOnly(nullptr,0);
    native_readonly_record.after=Hash(flags,sizeof(v8_flags));
    NEED(native_readonly_record.before==native_readonly_record.after);
    if (mode==4) {
        auto* request=(GtosDataReadonlyRequest*)seal_pages;
        *request={1,(unsigned)seal_pages,sizeof(seal_pages)};
        const unsigned before=Hash(seal_pages,sizeof(seal_pages));
        NEED(Request(GTOS_SYS_DATA_READONLY,*request)==0);
        NEED(Request(GTOS_SYS_DATA_READONLY,*request)==0);
        NEED(Hash(seal_pages,sizeof(seal_pages))==before);
        GtosVmRangeRequest undo={1,0,0,4096,GTOS_VM_READ_WRITE};
        NEED(Request(GTOS_SYS_VM_SET_PERMISSIONS,undo)==GTOS_VM_ERR_BAD_STATE);
    } else if (mode==0) {
        OS::SetDataReadOnly(seal_pages,sizeof(seal_pages));
        OS::SetDataReadOnly(seal_pages,sizeof(seal_pages));
        NEED(seal_pages[0]==17 && seal_pages[4096]==29);
    }
    if (mode==3) {
        native_readonly_record.go=1;
        while (native_readonly_record.worker_stage!=2) Yield();
    }
    if (mode==1 || mode==2 || mode==3) {
        native_readonly_record.fault_address=(unsigned)&v8_flags;
        native_readonly_record.stage=10;
        if (mode==1) *flags=original^1U;
        if (mode==3) native_readonly_record.go=2;
        for (;;) Yield();
    }
    native_readonly_record.stage=20;
    Call(GTOS_SYS_EXIT,0); for (;;) Yield();
}
}
extern "C" __attribute__((section(".text.entry"),noreturn)) void _start() {
    for (auto* init=__init_array_start;init!=__init_array_end;++init) (*init)();
    Main(); for (;;) Yield();
}
