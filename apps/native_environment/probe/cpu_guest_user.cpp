#include <process/abi.h>
#include <process/vm_abi.h>
#include <process/thread_abi.h>
#include "environment.h"
#include "cpu_guest_record.h"
extern "C" {
volatile CpuGuestRecord cpu_record __attribute__((section(".data.cpu_record"))) = {1,0,0,0,0,0,0,0,0,0,{0},{0},{0},{},{},{}};
}
static int Call(unsigned call, unsigned arg, unsigned bytes) {
    int status;
    asm volatile("int $0x80" : "=a"(status) : "0"(call), "b"(arg), "c"(bytes) : "memory", "cc");
    return status;
}
static void Exit(unsigned code) __attribute__((noreturn));
static void Exit(unsigned code) { Call(GTOS_SYS_EXIT,code,0); for (;;) {} }
static void Check(bool value, unsigned line) { if (!value) Exit(0xCC000000U | line); __atomic_fetch_add(&cpu_record.checks,1U,__ATOMIC_RELAXED); }
#define CHECK(v) Check((v),__LINE__)
static void Yield() { CHECK(Call(GTOS_SYS_YIELD,0,0)==0); }
static GtosCpuClockReadResult Read(unsigned scope) {
    GtosCpuClockReadResult result={};
    GtosCpuClockReadRequest request={1,scope,0,(unsigned)&result,sizeof(result)};
    CHECK(Call(GTOS_SYS_CPU_CLOCK_READ,(unsigned)&request,sizeof(request))==0);
    CHECK(result.version==1 && result.scope==scope && result.unit==1 && result.source==1
        && result.capabilities==7 && result.resolution_us==10000
        && result.pit_input_hz==1193182 && result.pit_divisor==11931);
    return result;
}
static void Copy(volatile GtosCpuClockReadResult& out, const GtosCpuClockReadResult& in) {
    const unsigned* s=(const unsigned*)&in; volatile unsigned* d=(volatile unsigned*)&out;
    for (unsigned i=0;i<12;++i) d[i]=s[i];
}
static void Fill(void* value,unsigned bytes) { for (unsigned i=0;i<bytes;++i) ((unsigned char*)value)[i]=0xA5; }
static bool Sentinel(const void* value,unsigned bytes) {
    for (unsigned i=0;i<bytes;++i) if (((const unsigned char*)value)[i]!=0xA5) return false;
    return true;
}
#include "environment_cases.h"
#include "sdk_cases.h"
static void Reject(GtosCpuClockReadRequest request, unsigned bytes, int expected) {
    unsigned char output[64]; Fill(output,sizeof(output)); request.result_va=(unsigned)(output+8);
    CHECK(Call(GTOS_SYS_CPU_CLOCK_READ,(unsigned)&request,bytes)==expected);
    CHECK(Sentinel(output,sizeof(output)));
}
static void Boundaries() {
    GtosCpuClockReadRequest request={1,1,0,0,48};
    Reject(request,19,-22); Reject(request,21,-22);
    request.version=2; Reject(request,20,-38); request.version=1;
    request.flags=1; Reject(request,20,-22); request.flags=0;
    request.result_bytes=47; Reject(request,20,-22); request.result_bytes=48;
    request.scope=0; Reject(request,20,-38); request.scope=3; Reject(request,20,-38); request.scope=1;
    CHECK(Call(GTOS_SYS_CPU_CLOCK_READ,0,20)==-14);
    CHECK(Call(GTOS_SYS_CPU_CLOCK_READ,0,19)==-22);
    const unsigned addresses[]={0,0x40000000U,0xBFFFCFF0U,0xFFFFFFF0U};
    for (unsigned i=0;i<4;++i) { request.result_va=addresses[i]; CHECK(Call(GTOS_SYS_CPU_CLOCK_READ,(unsigned)&request,20)==-14); }
    // Fixed record occupies the only data page; its last bytes are writable,
    // but the following page is unmapped. A partial copy must not occur.
    unsigned char* tail=(unsigned char*)(CPU_RECORD_VA+4096-16); Fill(tail,16);
    request.result_va=(unsigned)tail;
    CHECK(Call(GTOS_SYS_CPU_CLOCK_READ,(unsigned)&request,20)==-14 && Sentinel(tail,16));
    unsigned char overlap[80] __attribute__((aligned(4)));
    for (unsigned shift=0;shift<5;++shift) {
        Fill(overlap,sizeof(overlap));
        GtosCpuClockReadRequest* r=(GtosCpuClockReadRequest*)(overlap+8);
        r->version=1; r->scope=1; r->flags=0; r->result_va=(unsigned)(overlap+8+shift); r->result_bytes=48;
        CHECK(Call(GTOS_SYS_CPU_CLOCK_READ,(unsigned)r,20)==0);
        CHECK(Sentinel(overlap,8) && Sentinel(overlap+56+shift,24-shift));
    }
}
static void Busy(unsigned ticks) {
    const unsigned long long before=Read(1).charged_ticks;
    while (Read(1).charged_ticks-before<ticks) for (volatile unsigned i=0;i<50000;++i) {}
}
extern "C" void CpuWorker(unsigned role) {
    CHECK(role<3);
    CHECK(gtos_native_environment_initialize()==0);
    KnownEnvironment(); SsePattern(role+2);
    EnvironmentCases(false);
    cpu_record.initial[role]=(unsigned)Read(1).charged_ticks;
    __atomic_fetch_or(&cpu_record.workerStarted,1U<<role,__ATOMIC_RELAXED);
    Busy(3);
    cpu_record.final[role]=(unsigned)Read(1).charged_ticks;
    __atomic_fetch_or(&cpu_record.workerDone,1U<<role,__ATOMIC_RELAXED);
    Call(GTOS_SYS_THREAD_EXIT,40+role,0);
    for (;;) {}
}
static unsigned Start(unsigned role) {
    GtosVmReserveResult result={}; GtosVmReserveRequest reserve={1,16384,4096,0,(unsigned)&result};
    CHECK(Call(GTOS_SYS_VM_RESERVE,(unsigned)&reserve,sizeof(reserve))==0);
    GtosVmRangeRequest commit={1,result.handle,4096,8192,GTOS_VM_READ_WRITE};
    CHECK(Call(GTOS_SYS_VM_SET_PERMISSIONS,(unsigned)&commit,sizeof(commit))==0);
    unsigned id=0; GtosThreadCreateRequest create={1,0,(unsigned)&CpuWorker,role,result.handle,(unsigned)&id};
    CHECK(Call(GTOS_SYS_THREAD_CREATE,(unsigned)&create,sizeof(create))==0 && id);
    cpu_record.ids[role]=id;
    return result.handle;
}
static void Join(unsigned role,unsigned handle) {
    unsigned code=0; GtosThreadJoinRequest join={1,cpu_record.ids[role],(unsigned)&code};
    for (;;) { int status=Call(GTOS_SYS_THREAD_TRY_JOIN,(unsigned)&join,sizeof(join));
        if (!status) break; CHECK(status==GTOS_THREAD_ERR_BUSY); Yield(); }
    CHECK(code==40+role); ++cpu_record.joined;
    CHECK(Call(GTOS_SYS_THREAD_TRY_JOIN,(unsigned)&join,sizeof(join))==GTOS_THREAD_ERR_NO_THREAD);
    GtosVmControlRequest release={1,handle,0};
    CHECK(Call(GTOS_SYS_VM_RELEASE,(unsigned)&release,sizeof(release))==0);
}
extern "C" void NativeEntry() {
    CHECK(gtos_native_environment_initialize()==0);
    cpu_record.environmentPointer=(unsigned)getenv("FORCE_COLOR");
    startupReady=1;
    for (Initializer* p=__init_array_start;p!=__init_array_end;++p) (*p)();
    CHECK(cpu_record.constructorCalls==1 && gtos_native_environment_initialize()==0);
    cpu_record.phase=1;
    KnownEnvironment(); ValidatorCases(); SsePattern(cpu_record.mode==1?1:0);
    EnvironmentCases(true);
    if (cpu_record.mode==1) { for (;;) { KnownEnvironment(); SsePattern(1); EnvironmentCases(false); Busy(1); Yield(); } }
    Boundaries();
    unsigned first=Start(0),second=Start(1);
    if (cpu_record.mode==2) {
        while (cpu_record.workerStarted!=3) Yield();
        *(volatile unsigned*)0xBFFFCFFCU=1;
        Exit(99);
    }
    Copy(cpu_record.beforeJoin,Read(2));
    Join(0,first); Join(1,second);
    GtosCpuClockReadResult after=Read(2); Copy(cpu_record.afterJoin,after);
    CHECK(cpu_record.workerDone==3 && after.charged_ticks>=cpu_record.final[0]+cpu_record.final[1]);
    unsigned third=Start(2); Join(2,third);
    GtosCpuClockReadResult reused=Read(2); Copy(cpu_record.afterReuse,reused);
    CHECK(cpu_record.workerDone==7 && reused.charged_ticks>=after.charged_ticks+cpu_record.final[2]);
    cpu_record.phase=2; Exit(0);
}
