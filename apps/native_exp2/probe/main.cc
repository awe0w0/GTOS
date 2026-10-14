#include <process/abi.h>
#include <process/thread_abi.h>
#include <process/info_abi.h>
#include <process/vm_abi.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "src/__support/libc_errno.h"
#include "heap.h"
#include "thread-exit.h"
#include "record.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"
#include "src/base/timezone-cache.h"
extern "C" {
volatile TlsRecord native_tls_record __attribute__((section(".data.tls_record"))) = {1};
extern void* __dso_handle;
}
namespace {
using v8::base::Thread;
using v8::base::Semaphore;
using v8::base::TimeDelta;
using v8::base::OS;
using v8::base::TimezoneCache;
TimezoneCache* sharedZone;
double (*volatile Exp2)(double)=&exp2;
Thread::LocalStorageKey localKey;
Semaphore* gate;
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
bool Same(const char* actual,const char* expected) {
    while (*actual && *actual==*expected) { ++actual; ++expected; }
    return *actual==*expected;
}
int FormatList(char* buffer,int size,const char* format,...) {
    va_list args; va_start(args,format);
    int result=OS::VSNPrintF(buffer,size,format,args); va_end(args); return result;
}
unsigned ControlWord() { unsigned short word; asm volatile("fnstcw %0":"=m"(word)); return word; }
unsigned StatusWord() { unsigned short word; asm volatile("fnstsw %0":"=m"(word)); return word; }
unsigned Mxcsr() { unsigned word; asm volatile("stmxcsr %0":"=m"(word)); return word; }
void SetControls(unsigned cw,unsigned mx) {
    unsigned short word=(unsigned short)cw;
    asm volatile("fldcw %0; ldmxcsr %1"::"m"(word),"m"(mx):"memory");
}
void SetFlags(unsigned seed) {
    unsigned environment[7]; asm volatile("fnstenv %0":"=m"(environment)::"memory");
    environment[1]=(environment[1]&~0x3fU)|seed;
    asm volatile("fldenv %0"::"m"(environment):"memory");
    unsigned mx=(Mxcsr()&~0x3fU)|seed; asm volatile("ldmxcsr %0"::"m"(mx):"memory");
}
unsigned long long Bits(double value) { unsigned long long bits; memcpy(&bits,&value,8); return bits; }
double Value(unsigned long long bits) { double value; memcpy(&value,&bits,8); return value; }
#include "vectors.h"
void EnvironmentSanity(unsigned mode) {
    // One explicit SSE instruction separates arithmetic from syscall recovery.
    // For this special row the four bit fields record immediate, yield,
    // memcpy and errno-access MXCSR snapshots in that order.
    native_tls_record.math_mode=mode; native_tls_record.math_seed=0;
    native_tls_record.math_index=0xffffffffU;
    native_tls_record.math_input_hi=0x7fe00000U; native_tls_record.math_input_lo=0;
    native_tls_record.math_required=0x28;
    const double large=0x1.0p1023; double result; unsigned immediate;
    SetFlags(0);
    asm volatile("movsd %2,%%xmm0; addsd %%xmm0,%%xmm0; movsd %%xmm0,%0; stmxcsr %1"
        : "=m"(result),"=m"(immediate) : "m"(large) : "xmm0","memory");
    native_tls_record.math_actual_lo=immediate;
    Yield(); const unsigned afterYield=Mxcsr(); native_tls_record.math_actual_hi=afterYield;
    const unsigned long long bits=Bits(result);
    const unsigned afterCopy=Mxcsr(); native_tls_record.math_expected_lo=afterCopy;
    const int saved=errno; const unsigned afterErrno=Mxcsr(); native_tls_record.math_expected_hi=afterErrno;
    native_tls_record.math_cw=ControlWord(); native_tls_record.math_sw=StatusWord();
    native_tls_record.math_mx=afterErrno; native_tls_record.math_errno=(unsigned)saved;
    NEED((immediate&0x28)==0x28);
    NEED((afterYield&0x28)==0x28 && (afterCopy&0x28)==0x28 && (afterErrno&0x28)==0x28);
    NEED(bits==((mode==1 || mode==3) ? 0x7fefffffffffffffULL : 0x7ff0000000000000ULL));
}
void MathCase(unsigned mode,unsigned seed,unsigned index,unsigned long long input,
              unsigned long long expected,unsigned range,unsigned required,unsigned nan,int marker) {
    native_tls_record.math_mode=mode; native_tls_record.math_seed=seed; native_tls_record.math_index=index;
    native_tls_record.math_input_lo=(unsigned)input; native_tls_record.math_input_hi=(unsigned)(input>>32);
    const double x=Value(input); SetFlags(seed); errno=marker;
    NEED((StatusWord()&1)==seed && (Mxcsr()&1)==seed);
    const unsigned long long actual=Bits(Exp2(x));
    native_tls_record.math_cw=ControlWord(); native_tls_record.math_sw=StatusWord();
    native_tls_record.math_mx=Mxcsr(); native_tls_record.math_errno=(unsigned)errno;
    native_tls_record.math_required=required;
    native_tls_record.math_actual_lo=(unsigned)actual; native_tls_record.math_actual_hi=(unsigned)(actual>>32);
    native_tls_record.math_expected_lo=(unsigned)expected; native_tls_record.math_expected_hi=(unsigned)(expected>>32);
    NEED(nan ? (actual&0x7ff0000000000000ULL)==0x7ff0000000000000ULL && (actual&0xfffffffffffffULL) : actual==expected);
    NEED(errno==(range ? ERANGE : marker));
    NEED((ControlWord()&0xc00)==mode<<10 && (Mxcsr()&0x6000)==mode<<13);
    NEED(((StatusWord()|Mxcsr())&required)==required);
    if ((input&0x7fffffffffffffffULL)<=0x0010000000000000ULL)
        NEED(((StatusWord()|Mxcsr())&0x1c)==0);
    NEED((StatusWord()&1)==seed && (Mxcsr()&1)==seed);
    if ((index&7)==0) {
        Yield(); NEED(errno==(range ? ERANGE : marker));
        NEED((ControlWord()&0xc00)==mode<<10 && (Mxcsr()&0x6000)==mode<<13);
        NEED((StatusWord()&1)==seed && (Mxcsr()&1)==seed);
    }
    asm volatile("lock incl %0" : "+m"(native_tls_record.math_calls) : : "cc", "memory");
}
void Exp2Checks(bool exhaustive) {
    static_assert(sizeof(double)==8 && sizeof(unsigned long long)==8,"actual IEEE64 ABI");
    const int saved=errno,marker=14000+OS::GetCurrentThreadId();
    const unsigned cw=ControlWord(),mx=Mxcsr();
    unsigned environment[7]; asm volatile("fnstenv %0; fldenv %0":"=m"(environment)::"memory");
    for (unsigned mode=0;mode<4;++mode) {
        SetControls((cw&~0xc00U)|(mode<<10),(mx&~0xe07fU)|(mode<<13));
        EnvironmentSanity(mode);
        for (unsigned seed=0;seed<2;++seed) {
            unsigned index=0;
            for (const auto& row : exp2Cases) {
                MathCase(mode,seed,index++,row.input,row.expected[mode],row.range[mode],row.required[mode],row.nan,marker);
            }
        }
        // Independent exact powers cover all representable integer exponents.
        for (int e=-1074;exhaustive && e<=1023;++e) {
            const unsigned long long expected=e<-1022 ? 1ULL<<(e+1074) : (unsigned long long)(e+1023)<<52;
            MathCase(mode,0,(unsigned)(e+1074),Bits((double)e),expected,0,0,0,marker);
        }
    }
    asm volatile("fldenv %0"::"m"(environment):"memory"); SetControls(cw,mx); errno=saved;
}
void PlatformCore(bool exhaustive=false) {
    Exp2Checks(exhaustive);
    OS::AdjustSchedulingParams();
    TimezoneCache* first=OS::CreateTimezoneCache();
    TimezoneCache* second=OS::CreateTimezoneCache();
    NEED(first && second && first!=second && first!=sharedZone && second!=sharedZone);
    const double values[]={0,-0.0,1,-1,8640000000000000.0,-8640000000000000.0,
        __DBL_MAX__,-__DBL_MAX__,__builtin_nan(""),__builtin_inf(),-__builtin_inf()};
    for (TimezoneCache* zone : {first,second,sharedZone}) {
        for (double value : values) {
            NEED(Same(zone->LocalTimezone(value),__builtin_isfinite(value) ? "UTC" : ""));
            NEED(zone->DaylightSavingsOffset(value)==0);
            NEED(zone->LocalTimeOffset(value,true)==0 && zone->LocalTimeOffset(value,false)==0);
        }
        zone->Clear(TimezoneCache::TimeZoneDetection::kSkip);
        NEED(Same(zone->LocalTimezone(0),"UTC"));
        zone->Clear(TimezoneCache::TimeZoneDetection::kRedetect);
        NEED(Same(zone->LocalTimezone(-1),"UTC"));
    }
    const char* name=second->LocalTimezone(0); delete first;
    NEED(Same(name,"UTC") && Same(second->LocalTimezone(0),"UTC")); delete second;
    char storage[34];
    for (char& byte : storage) byte=(char)0xa5;
    NEED(OS::SNPrintF(storage+1,32,"n=%d x=%x s=%s",42,0xab,"ok")==14);
    NEED(Same(storage+1,"n=42 x=ab s=ok") && (unsigned char)storage[0]==0xa5 && (unsigned char)storage[33]==0xa5);
    NEED(FormatList(storage+1,32,"%.2f:%lld",1.25,1234567890123LL)==18);
    NEED(Same(storage+1,"1.25:1234567890123"));
    for (char& byte : storage) byte=(char)0xa5;
    NEED(OS::SNPrintF(storage+1,8,"%s","1234567")==7 && Same(storage+1,"1234567"));
    NEED((unsigned char)storage[0]==0xa5 && (unsigned char)storage[9]==0xa5);
    NEED(OS::SNPrintF(storage+1,4,"%s","abcdef")==-1 && Same(storage+1,"abc"));
    NEED(storage[4]==0 && (unsigned char)storage[0]==0xa5);
    NEED(FormatList(storage+1,1,"%s","a")==-1 && storage[1]==0);
    NEED(OS::SNPrintF(storage+1,1,"%s","")==0 && storage[1]==0);
    for (char& byte : storage) byte=(char)0xa5;
    NEED(OS::SNPrintF(nullptr,0,"%d",42)==-1);
    NEED(OS::SNPrintF(storage+1,-1,"%s","abc")==-1);
    for (char byte : storage) NEED((unsigned char)byte==0xa5);
}
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
    NEED(Thread::GetThreadLocal(localKey)==(void*)Expected(index));
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
    NEED(Thread::GetThreadLocal(localKey)==nullptr);
    Thread::SetThreadLocal(localKey,(void*)Expected(index));
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
    gate->Wait();
    NEED(Thread::GetThreadLocal(localKey)==nullptr);
    Thread::SetThreadLocal(localKey,(void*)Expected(index));
    Exercise(index); native_tls_record.worker[index].stage=2;
    if (native_tls_record.mode) {
        while (native_tls_record.go!=2) Yield();
        if (index==0 && native_tls_record.mode==2) ExhaustTls();
        if (index==0 && native_tls_record.mode==3) *(volatile unsigned*)0x70000000=1;
        for (;;) Yield();
    }
}
class ActualThread final : public Thread {
public:
    explicit ActualThread(unsigned index,int size=8192) : Thread(Options("gtos-v8-probe",size)), index_(index) {}
    void Run() override {
        GtosProcessInfoResult info={}; GtosProcessInfoRequest self={1,0,(unsigned)&info,sizeof(info)};
        NEED(Request(GTOS_SYS_PROCESS_INFO,self)==0 && info.thread_id!=info.process_id);
        native_tls_record.worker[index_].id=info.thread_id;
        unsigned stackMarker=0;
        NEED((unsigned)&stackMarker>=info.stack_begin && (unsigned)&stackMarker<info.stack_end);
        NEED(info.stack_end-info.stack_begin==(index_==0 ? 65536U : 8192U));
        PlatformCore();
        Worker(index_);
    }
private:
    unsigned index_;
};
void Main() {
    GtosProcessInfoResult info={}; GtosProcessInfoRequest self={1,0,(unsigned)&info,sizeof(info)};
    NEED(Request(GTOS_SYS_PROCESS_INFO,self)==0 && info.thread_id==info.process_id);
    native_tls_record.parent=info.process_id;
    NEED(errno==0); GtosVmRegionInfo empty={}; NEED(Query(&empty)==0);
    sharedZone=OS::CreateTimezoneCache(); NEED(sharedZone!=nullptr); PlatformCore(info.maximum_parallel_threads==1);
    if (info.maximum_parallel_threads==1) {
        for (unsigned i=0;i<8;++i) {
            ActualThread unavailable(0,0);
            NEED(!unavailable.StartSynchronously()); NEED(!unavailable.Start());
        }
        {
            Semaphore available(1);
            NEED(available.WaitFor(TimeDelta::FromMicroseconds(0)));
            NEED(!available.WaitFor(TimeDelta::FromMicroseconds(0)));
            localKey=Thread::CreateThreadLocalKey();
            NEED(Thread::GetThreadLocal(localKey)==nullptr);
            Thread::SetThreadLocal(localKey,(void*)3);
            auto old=localKey; Thread::DeleteThreadLocalKey(old);
            localKey=Thread::CreateThreadLocalKey();
            NEED(localKey!=old && Thread::GetThreadLocal(localKey)==nullptr);
            Thread::SetThreadLocal(localKey,(void*)4);
            NEED(Thread::GetThreadLocal(localKey)==(void*)4);
            Thread::DeleteThreadLocalKey(localKey);
        }
        delete sharedZone; sharedZone=nullptr; gtos_native_thread_cleanup();
        NEED(Query(&empty)==0); native_tls_record.emptyHeap=1;
        native_tls_record.stage=20; Call(GTOS_SYS_EXIT,0); for (;;) Yield();
    }
    NEED(info.maximum_parallel_threads==4);
    localKey=Thread::CreateThreadLocalKey(); Initialize(3);
    for (unsigned wave=0;wave<(native_tls_record.mode ? 1U : TLS_WAVES);++wave) {
        native_tls_record.wave=wave; native_tls_record.go=0;
        Semaphore permits(0); gate=&permits;
        NEED(!permits.WaitFor(TimeDelta::FromMicroseconds(0)));
        NEED(!permits.WaitFor(TimeDelta::FromMicroseconds(-1)));
        NEED(!permits.WaitFor(TimeDelta::FromMicroseconds(1000)));
        permits.Signal(); NEED(permits.WaitFor(TimeDelta::FromMicroseconds(0)));
        NEED(!permits.WaitFor(TimeDelta::FromMicroseconds(0)));
        ActualThread first(0,0),second(1),third(2);
        ActualThread* threads[]={&first,&second,&third};
        for (unsigned index=0;index<3;++index) {
            for (unsigned byte=0;byte<sizeof(TlsWorkerRecord);++byte)
                ((volatile unsigned char*)&native_tls_record.worker[index])[byte]=0;
            NEED(threads[index]->StartSynchronously());
            NEED(!threads[index]->Start());
            auto& r=native_tls_record.worker[index];
            while (!r.stage) Yield();
        }
        { ActualThread extra(0); NEED(!extra.StartSynchronously()); }
        { ActualThread invalid(0,-1); NEED(!invalid.StartSynchronously()); }
        for (unsigned i=0;i<4;++i) for (unsigned j=i+1;j<4;++j) {
            NEED(native_tls_record.worker[i].valueAddress!=native_tls_record.worker[j].valueAddress);
            NEED(native_tls_record.worker[i].blockAddress!=native_tls_record.worker[j].blockAddress);
        }
        auto old=localKey; Thread::DeleteThreadLocalKey(old);
        localKey=Thread::CreateThreadLocalKey();
        NEED(localKey!=old && Thread::GetThreadLocal(localKey)==nullptr);
        Thread::SetThreadLocal(localKey,(void*)Expected(3));
        native_tls_record.go=1;
        for (unsigned count=1;count<=3;++count) {
            permits.Signal();
            for (;;) {
                unsigned stopped=0;
                for (unsigned i=0;i<3;++i) if (native_tls_record.worker[i].stage==2) ++stopped;
                NEED(stopped<=count);
                if (stopped==count) break;
                Yield();
            }
        }
        Exercise(3);
        if (native_tls_record.mode) {
            for (unsigned i=0;i<3;++i) while (native_tls_record.worker[i].stage!=2) Yield();
            native_tls_record.stage=10;
            if (native_tls_record.mode!=1) native_tls_record.go=2;
            for (;;) Yield();
        }
        for (unsigned i=0;i<3;++i) {
            auto& r=native_tls_record.worker[i]; threads[i]->Join();
            NEED(r.constructions==3 && r.destructions==3 && r.callbacks==1 && r.order==2341 && r.rounds==16);
            native_tls_record.joins=native_tls_record.joins+1;
        }
        CheckOwned(3);
    }
    NEED(native_tls_record.joins==3*TLS_WAVES);
    delete sharedZone; sharedZone=nullptr;
    gtos_native_thread_cleanup();
    Thread::DeleteThreadLocalKey(localKey);
    const auto& root=native_tls_record.worker[3];
    NEED(root.constructions==3 && root.destructions==3 && root.callbacks==1 && root.order==2341 && root.rounds==16*TLS_WAVES);
    NEED(Query(&empty)==0); native_tls_record.emptyHeap=1;
    native_tls_record.stage=20; Call(GTOS_SYS_EXIT,0); for (;;) Yield();
}
}
extern "C" __attribute__((section(".text.entry"),noreturn)) void _start() { Main(); for (;;) Yield(); }
