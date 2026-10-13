#include <process/abi.h>
#include <process/thread_abi.h>
#include <process/info_abi.h>
#include <process/vm_abi.h>
#include <stdlib.h>
#include "heap.h"
#include "record.h"
extern "C" {
volatile HeapRecord native_heap_record __attribute__((section(".data.heap_record"))) = {1};
int posix_memalign(void**, size_t, size_t) noexcept;
}
namespace {
auto volatile Malloc = &malloc;
auto volatile Calloc = &calloc;
auto volatile Realloc = &realloc;
auto volatile Free = &free;
auto volatile Aligned = &posix_memalign;
auto volatile Query = &gtos_native_heap_query;
int Call(unsigned op, unsigned a=0, unsigned b=0) {
    asm volatile("int $0x80" : "+a"(op) : "b"(a), "c"(b) : "memory", "cc"); return (int)op;
}
template <class T> int Request(unsigned op, T& r) { return Call(op, (unsigned)&r, sizeof(r)); }
void Yield() { Call(GTOS_SYS_YIELD); }
void Need(bool pass, unsigned line) {
    asm volatile("lock incl %0" : "+m"(native_heap_record.checks) : : "cc", "memory");
    if (!pass) { native_heap_record.error=line; Call(GTOS_SYS_EXIT, line); for (;;) Yield(); }
}
#define NEED(x) Need((x), __LINE__)
void Phase(unsigned index, unsigned value) { native_heap_record.worker[index].phase=value; asm volatile("" : : : "memory"); }
void Check(const unsigned char* p, unsigned n, unsigned char value) {
    for (unsigned i=0; i<n; ++i) NEED(p[i]==value);
}
void Fill(unsigned char* p, unsigned n, unsigned char value) { for (unsigned i=0; i<n; ++i) p[i]=value; }
void Round(unsigned index, unsigned round) {
    const unsigned length=49+round%17; const unsigned char value=(unsigned char)(index*67+round);
    Phase(index,1); auto* p=(unsigned char*)Malloc(length); NEED(p);
    auto* blocker=(unsigned char*)Malloc(80); NEED(blocker); Phase(index,2);
    Fill(p,length,value); Fill(blocker,80,(unsigned char)(value^0xff)); Yield();
    Check(p,length,value); Check(blocker,80,(unsigned char)(value^0xff));
    GtosVmRegionInfo info={}; Phase(index,1); NEED(Query(&info)==1 && info.resident_pages==16);
    native_heap_record.heapBase=info.base; Phase(index,2);
    Phase(index,1); auto* grown=(unsigned char*)Realloc(p,length+257); NEED(grown); Phase(index,2);
    if (grown!=p) native_heap_record.worker[index].moved=native_heap_record.worker[index].moved+1;
    Check(grown,length,value); Fill(grown,length+257,value); Yield(); Check(grown,length+257,value);
    Phase(index,1); NEED(Realloc(grown,65536)==nullptr); Phase(index,2); Check(grown,length+257,value);
    Phase(index,1); auto* zero=(unsigned char*)Calloc(5,37); NEED(zero); Phase(index,2); Check(zero,185,0);
    void* aligned=nullptr; Phase(index,1); NEED(Aligned(&aligned,256,43)==0 && aligned && !((unsigned)aligned&255)); Phase(index,2);
    Fill((unsigned char*)aligned,43,value); Yield(); Check(grown,length+257,value); Check((unsigned char*)aligned,43,value);
    Phase(index,1); Free(aligned); Free(zero); Free(blocker); NEED(Realloc(grown,0)==nullptr); Phase(index,2);
    native_heap_record.worker[index].rounds=native_heap_record.worker[index].rounds+1;
}
void Worker(unsigned index) {
    NEED(index<3);
    native_heap_record.worker[index].stage=1;
    while (!native_heap_record.go) Yield();
    if (native_heap_record.mode) {
        Phase(index,1); void* p=Malloc(4096); NEED(p); Phase(index,2);
        Fill((unsigned char*)p,4096,(unsigned char)(index+0x41));
        native_heap_record.worker[index].held=(unsigned)p; native_heap_record.worker[index].stage=3;
        while (native_heap_record.go!=2) Yield();
        if (native_heap_record.mode==2 && index==0) {
            Phase(index,1); Aligned((void**)0x70000000,64,32);
            NEED(false);
        }
        for (;;) Yield();
    }
    for (unsigned round=0; round<HEAP_ROUNDS; ++round) Round(index,round);
    NEED(native_heap_record.worker[index].moved>0);
    native_heap_record.worker[index].stage=2; Phase(index,2);
    Call(GTOS_SYS_THREAD_EXIT,90+index); for (;;) Yield();
}
void Main() {
    GtosProcessInfoResult info={}; GtosProcessInfoRequest self={1,0,(unsigned)&info,sizeof(info)};
    NEED(Request(GTOS_SYS_PROCESS_INFO,self)==0 && info.maximum_parallel_threads==4 && info.thread_id==info.process_id);
    native_heap_record.parent=info.process_id;
    for (unsigned index=0; index<3; ++index) {
        GtosVmReserveResult result={}; GtosVmReserveRequest reserve={1,16384,4096,0,(unsigned)&result};
        NEED(Request(GTOS_SYS_VM_RESERVE,reserve)==0);
        native_heap_record.worker[index].stackHandle=result.handle; native_heap_record.worker[index].stackBase=result.base;
        GtosVmRangeRequest rw={1,result.handle,4096,8192,GTOS_VM_READ_WRITE}; NEED(Request(GTOS_SYS_VM_SET_PERMISSIONS,rw)==0);
        GtosThreadCreateRequest create={1,0,(unsigned)&Worker,index,result.handle,(unsigned)&native_heap_record.worker[index].id};
        NEED(Request(GTOS_SYS_THREAD_CREATE,create)==0);
        while (!native_heap_record.worker[index].stage) Yield();
    }
    native_heap_record.go=1;
    if (native_heap_record.mode) {
        for (unsigned index=0; index<3; ++index) while (native_heap_record.worker[index].stage!=3) Yield();
        native_heap_record.stage=10;
        if (native_heap_record.mode==2) native_heap_record.go=2;
        for (;;) Yield();
    }
    for (unsigned round=0; round<HEAP_ROUNDS; ++round) Round(3,round);
    for (unsigned index=0; index<3; ++index) {
        unsigned code=0; GtosThreadJoinRequest join={1,native_heap_record.worker[index].id,(unsigned)&code}; int result;
        do { result=Request(GTOS_SYS_THREAD_TRY_JOIN,join); if (result==GTOS_THREAD_ERR_BUSY) Yield(); } while (result==GTOS_THREAD_ERR_BUSY);
        NEED(result==0 && code==90+index);
        GtosVmControlRequest release={1,native_heap_record.worker[index].stackHandle,0}; NEED(Request(GTOS_SYS_VM_RELEASE,release)==0);
    }
    GtosVmRegionInfo empty={}; Phase(3,1); NEED(Query(&empty)==0); Phase(3,2);
    for (unsigned index=0; index<4; ++index) NEED(native_heap_record.worker[index].rounds==HEAP_ROUNDS && native_heap_record.worker[index].moved);
    native_heap_record.stage=20; Call(GTOS_SYS_EXIT,0); for (;;) Yield();
}
}
extern "C" __attribute__((section(".text.entry"),noreturn)) void _start() { Main(); for (;;) Yield(); }
