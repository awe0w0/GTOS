#include <process/abi.h>
#include <process/vm_abi.h>
#include <process/thread_abi.h>
#include "record.h"
extern "C" {
volatile CpuGuestRecord cpu_record __attribute__((section(".data.cpu_record")))={1,0,0,0,0,0,0,{0},0,0,0,0,0,{0},0,0};
}
static int Call(unsigned op,unsigned arg,unsigned bytes) {
    int result;asm volatile("int $0x80":"=a"(result):"0"(op),"b"(arg),"c"(bytes):"memory","cc");return result;
}
static void Exit(unsigned code) __attribute__((noreturn));
static void Exit(unsigned code) {Call(GTOS_SYS_EXIT,code,0);for (;;) {}}
static void Check(bool value,unsigned line) {if (!value) Exit(0xCC000000U|line);__atomic_fetch_add(&cpu_record.checks,1U,__ATOMIC_RELAXED);}
#define CHECK(value) Check((value),__LINE__)
static void Yield() {CHECK(Call(GTOS_SYS_YIELD,0,0)==0);}
static void Fill(void* value,unsigned bytes) {for (unsigned i=0;i<bytes;++i) ((unsigned char*)value)[i]=0xA5;}
static bool Sentinel(const void* value,unsigned bytes) {for (unsigned i=0;i<bytes;++i) if (((const unsigned char*)value)[i]!=0xA5) return false;return true;}
#include "file_cases.h"
static void SsePattern(unsigned role) {
    unsigned in[4] __attribute__((aligned(16)))={0xC0010000U+role,0xABCD1000U+role,0x56780000U+role,0xA5A50000U+role};
    unsigned out[4] __attribute__((aligned(16)))={};
    const unsigned mx=0x1F80U|((role&3)<<13);unsigned observedMx=0;
    const unsigned short cw=(unsigned short)(0x037FU|((role&3)<<10));unsigned short observedCw=0;
    int status;
    asm volatile("ldmxcsr %[mx]; fldcw %[cw]; movdqa %[source],%%xmm0; int $0x80; movdqa %%xmm0,%[destination]; stmxcsr %[mxout]; fnstcw %[cwout]"
        :"=a"(status),[cwout]"=m"(observedCw),[destination]"=m"(out),[mxout]"=m"(observedMx)
        :"0"(GTOS_SYS_YIELD),[cw]"m"(cw),[source]"m"(in),[mx]"m"(mx),"b"(0),"c"(0):"memory","cc","xmm0");
    CHECK(status==0&&observedMx==mx&&observedCw==cw);
    for (unsigned i=0;i<4;++i) CHECK(out[i]==in[i]);
}
extern "C" void DupWorker(unsigned role) {
    CHECK(role<3);SsePattern(role+2);FileWorker(role);
    __atomic_fetch_or(&cpu_record.workerStarted,1U<<role,__ATOMIC_RELAXED);
    for (unsigned i=0;i<8;++i) SsePattern(role+5);
    __atomic_fetch_or(&cpu_record.workerDone,1U<<role,__ATOMIC_RELAXED);
    Call(GTOS_SYS_THREAD_EXIT,40+role,0);for (;;) {}
}
static unsigned Start(unsigned role) {
    GtosVmReserveResult region={};GtosVmReserveRequest reserve={1,16384,4096,0,(unsigned)&region};
    CHECK(Call(GTOS_SYS_VM_RESERVE,(unsigned)&reserve,sizeof(reserve))==0);
    GtosVmRangeRequest commit={1,region.handle,4096,8192,GTOS_VM_READ_WRITE};
    CHECK(Call(GTOS_SYS_VM_SET_PERMISSIONS,(unsigned)&commit,sizeof(commit))==0);
    unsigned id=0;GtosThreadCreateRequest create={1,0,(unsigned)&DupWorker,role,region.handle,(unsigned)&id};
    CHECK(Call(GTOS_SYS_THREAD_CREATE,(unsigned)&create,sizeof(create))==0&&id);cpu_record.ids[role]=id;return region.handle;
}
static void Join(unsigned role,unsigned stack) {
    unsigned code=0;GtosThreadJoinRequest join={1,cpu_record.ids[role],(unsigned)&code};
    for (;;) {int result=Call(GTOS_SYS_THREAD_TRY_JOIN,(unsigned)&join,sizeof(join));if (!result) break;CHECK(result==GTOS_THREAD_ERR_BUSY);Yield();}
    CHECK(code==40+role);++cpu_record.joined;
    CHECK(Call(GTOS_SYS_THREAD_TRY_JOIN,(unsigned)&join,sizeof(join))==GTOS_THREAD_ERR_NO_THREAD);
    GtosVmControlRequest release={1,stack,0};CHECK(Call(GTOS_SYS_VM_RELEASE,(unsigned)&release,sizeof(release))==0);
}
extern "C" void NativeEntry() {
    CHECK(cpu_record.version==1&&Call(GTOS_SYS_ABI,0,0)==1);cpu_record.phase=1;
    if (cpu_record.mode==1) {FilePeer();for (;;) {FilePeerPulse();SsePattern(1);}}
    CHECK(cpu_record.mode==0||cpu_record.mode==2);SsePattern(0);FileRoot();
    unsigned first=Start(0),second=Start(1);
    if (cpu_record.mode==2) {while (cpu_record.workerStarted!=3) Yield();*(volatile unsigned*)0xBFFFCFFCU=1;Exit(99);}
    Join(0,first);Join(1,second);CHECK(cpu_record.workerDone==3);
    unsigned third=Start(2);Join(2,third);CHECK(cpu_record.workerDone==7);FileFinish();cpu_record.phase=2;Exit(0);
}
