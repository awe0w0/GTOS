#include <process/abi.h>
#include <process/thread_abi.h>
#include <process/info_abi.h>
#include <process/vm_abi.h>
#include <errno.h>
#include <stdlib.h>
#include "src/__support/libc_errno.h"
#include "heap.h"
#include "thread-exit.h"
#include "record.h"
extern "C" {
volatile TlsRecord native_tls_record __attribute__((section(".data.tls_record"))) = {1};
extern void* __dso_handle;
}
namespace {
thread_local unsigned localValue = 17;
alignas(256) thread_local unsigned char localBlock[257] = {3};
auto volatile Query = &gtos_native_heap_query;
int Call(unsigned op, unsigned a=0, unsigned b=0) {
    asm volatile("int $0x80" : "+a"(op) : "b"(a), "c"(b) : "memory", "cc"); return (int)op;
}
template <class T> int Request(unsigned op, T& r) { return Call(op, (unsigned)&r, sizeof(r)); }
void Yield() { Call(GTOS_SYS_YIELD); }
void Need(bool pass, unsigned line) {
    asm volatile("lock incl %0" : "+m"(native_tls_record.checks) : : "cc", "memory");
    if (!pass) { native_tls_record.error=line; Call(GTOS_SYS_EXIT,line); for (;;) Yield(); }
}
#define NEED(x) Need((x), __LINE__)
unsigned CurrentIndex() {
    GtosProcessInfoResult info={}; GtosProcessInfoRequest self={1,0,(unsigned)&info,sizeof(info)};
    NEED(Request(GTOS_SYS_PROCESS_INFO,self)==0);
    if (info.thread_id==native_tls_record.parent) return 3;
    for (unsigned i=0;i<3;++i) if (info.thread_id==native_tls_record.worker[i].id) return i;
    NEED(false); return 0;
}
unsigned Expected(unsigned index) { return 1000+index+(index==3 ? 0 : native_tls_record.wave*10); }
void CheckOwned(unsigned index) {
    NEED(CurrentIndex()==index && localValue==Expected(index));
    NEED(errno==(int)(Expected(index)+500));
    NEED((int)__llvm_libc_cr::libc_errno==(int)(Expected(index)+500));
}
void Append(unsigned index,unsigned tag) {
    auto& r=native_tls_record.worker[index]; r.order=r.order*10+tag;
}
void Callback(void* pointer) {
    unsigned index=(unsigned)pointer; CheckOwned(index); Append(index,3);
    auto& r=native_tls_record.worker[index]; r.callbacks=r.callbacks+1;
}
struct Tracked {
    unsigned owner, tag;
    explicit Tracked(unsigned value) : owner(CurrentIndex()), tag(value) {
        auto& r=native_tls_record.worker[owner]; r.constructions=r.constructions+1;
    }
    ~Tracked();
};
Tracked& First() { static thread_local Tracked object(1); return object; }
Tracked& Second() { static thread_local Tracked object(2); return object; }
Tracked& Late() { static thread_local Tracked object(4); return object; }
Tracked::~Tracked() {
    CheckOwned(owner); Append(owner,tag);
    auto& r=native_tls_record.worker[owner]; r.destructions=r.destructions+1;
    if (tag==2) {
        NEED(Late().owner==owner);
        NEED(__cxa_thread_atexit(Callback,(void*)owner,__dso_handle)==0);
    }
    Yield();
}
template <unsigned N> unsigned& Slot() { static thread_local unsigned value=N+100; return value; }
template <unsigned N> void Slots(unsigned index,bool initial) {
    unsigned& value=Slot<N>();
    NEED(value==(initial ? N+100 : Expected(index)+N));
    if (initial) value=Expected(index)+N;
    Slots<N-1>(index,initial);
}
template <> void Slots<0>(unsigned index,bool initial) {
    unsigned& value=Slot<0>(); NEED(value==(initial ? 100 : Expected(index)));
    if (initial) value=Expected(index);
}
void Initialize(unsigned index) {
    // Errno must register an owner before the first allocator/TLS operation.
    NEED(errno==0 && (int)__llvm_libc_cr::libc_errno==0);
    __llvm_libc_cr::libc_errno=(int)(Expected(index)+500);
    NEED(errno==(int)(Expected(index)+500));
    NEED(localValue==17 && localBlock[0]==3 && !((unsigned)localBlock&255));
    for (unsigned i=1;i<257;++i) NEED(localBlock[i]==0);
    localValue=Expected(index); localBlock[256]=(unsigned char)(index+21);
    auto& r=native_tls_record.worker[index]; r.valueAddress=(unsigned)&localValue; r.blockAddress=(unsigned)localBlock;
    NEED(First().owner==index && Second().owner==index);
    Slots<31>(index,true); CheckOwned(index); r.stage=1;
}
void Exercise(unsigned index) {
    for (unsigned n=0;n<16;++n) {
        Yield(); CheckOwned(index); Slots<31>(index,false);
        NEED(localBlock[0]==3 && localBlock[256]==index+21);
        auto& r=native_tls_record.worker[index]; r.rounds=r.rounds+1;
    }
}
void ExhaustTls() {
    static thread_local unsigned char tooLarge[65536];
    auto* volatile pointer=&tooLarge[0]; *pointer=1;
    NEED(false);
}
void Worker(unsigned index) {
    NEED(index<3); Initialize(index);
    while (!native_tls_record.go) Yield();
    Exercise(index); native_tls_record.worker[index].stage=2;
    if (native_tls_record.mode) {
        while (native_tls_record.go!=2) Yield();
        if (index==0 && native_tls_record.mode==2) ExhaustTls();
        if (index==0 && native_tls_record.mode==3) *(volatile unsigned*)0x70000000=1;
        for (;;) Yield();
    }
    gtos_native_exit_with_tls(90+index);
}
void Main() {
    GtosProcessInfoResult info={}; GtosProcessInfoRequest self={1,0,(unsigned)&info,sizeof(info)};
    NEED(Request(GTOS_SYS_PROCESS_INFO,self)==0 && info.maximum_parallel_threads==4 && info.thread_id==info.process_id);
    native_tls_record.parent=info.process_id;
    NEED(errno==0); GtosVmRegionInfo empty={}; NEED(Query(&empty)==0);
    Initialize(3);
    for (unsigned wave=0;wave<(native_tls_record.mode ? 1U : TLS_WAVES);++wave) {
        native_tls_record.wave=wave; native_tls_record.go=0;
        for (unsigned index=0;index<3;++index) {
            for (unsigned byte=0;byte<sizeof(TlsWorkerRecord);++byte)
                ((volatile unsigned char*)&native_tls_record.worker[index])[byte]=0;
            GtosVmReserveResult result={}; GtosVmReserveRequest reserve={1,16384,4096,0,(unsigned)&result};
            NEED(Request(GTOS_SYS_VM_RESERVE,reserve)==0);
            auto& r=native_tls_record.worker[index]; r.stackHandle=result.handle; r.stackBase=result.base;
            GtosVmRangeRequest rw={1,result.handle,4096,8192,GTOS_VM_READ_WRITE}; NEED(Request(GTOS_SYS_VM_SET_PERMISSIONS,rw)==0);
            GtosThreadCreateRequest create={1,0,(unsigned)&Worker,index,result.handle,(unsigned)&r.id};
            NEED(Request(GTOS_SYS_THREAD_CREATE,create)==0);
            while (!r.stage) Yield();
        }
        for (unsigned i=0;i<4;++i) for (unsigned j=i+1;j<4;++j) {
            NEED(native_tls_record.worker[i].valueAddress!=native_tls_record.worker[j].valueAddress);
            NEED(native_tls_record.worker[i].blockAddress!=native_tls_record.worker[j].blockAddress);
        }
        native_tls_record.go=1; Exercise(3);
        if (native_tls_record.mode) {
            for (unsigned i=0;i<3;++i) while (native_tls_record.worker[i].stage!=2) Yield();
            native_tls_record.stage=10;
            if (native_tls_record.mode!=1) native_tls_record.go=2;
            for (;;) Yield();
        }
        for (unsigned i=0;i<3;++i) {
            auto& r=native_tls_record.worker[i]; unsigned code=0; GtosThreadJoinRequest join={1,r.id,(unsigned)&code}; int result;
            do { result=Request(GTOS_SYS_THREAD_TRY_JOIN,join); if (result==GTOS_THREAD_ERR_BUSY) Yield(); } while (result==GTOS_THREAD_ERR_BUSY);
            NEED(result==0 && code==90+i && r.constructions==3 && r.destructions==3 && r.callbacks==1 && r.order==2341 && r.rounds==16);
            GtosVmControlRequest release={1,r.stackHandle,0}; NEED(Request(GTOS_SYS_VM_RELEASE,release)==0);
            native_tls_record.joins=native_tls_record.joins+1;
        }
        CheckOwned(3);
    }
    NEED(native_tls_record.joins==3*TLS_WAVES);
    gtos_native_thread_cleanup();
    const auto& root=native_tls_record.worker[3];
    NEED(root.constructions==3 && root.destructions==3 && root.callbacks==1 && root.order==2341 && root.rounds==16*TLS_WAVES);
    NEED(Query(&empty)==0); native_tls_record.emptyHeap=1;
    native_tls_record.stage=20; Call(GTOS_SYS_EXIT,0); for (;;) Yield();
}
}
extern "C" __attribute__((section(".text.entry"),noreturn)) void _start() { Main(); for (;;) Yield(); }
