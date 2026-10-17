#include <process/abi.h>
#include <process/info_abi.h>
#include <process/thread_abi.h>
#include <stdlib.h>
#include "heap.h"
#include "thread-exit.h"
#include "src/base/platform/platform.h"
#include "api.h"
#include "record.h"
extern "C" {
volatile CryptoThreadRecord native_crypto_thread_record __attribute__((section(".data.crypto_thread_record"))) = {1};
}
namespace {
using v8::base::Thread;
unsigned Load(const volatile unsigned& value) { return __atomic_load_n(&value,__ATOMIC_ACQUIRE); }
void Store(volatile unsigned& value,unsigned number) { __atomic_store_n(&value,number,__ATOMIC_RELEASE); }
unsigned Add(volatile unsigned& value,unsigned number=1) { return __atomic_add_fetch(&value,number,__ATOMIC_ACQ_REL); }
int Call(unsigned op,unsigned a=0,unsigned b=0) {
    asm volatile("int $0x80" : "+a"(op) : "b"(a), "c"(b) : "memory", "cc"); return (int)op;
}
template <class T> int Request(unsigned op,T& request) { return Call(op,(unsigned)&request,sizeof(request)); }
void Yield() { Call(GTOS_SYS_YIELD); }
void Need(bool pass,unsigned line) {
    Add(native_crypto_thread_record.checks);
    if (!pass) {
        unsigned empty=0; __atomic_compare_exchange_n(&native_crypto_thread_record.error,&empty,line,false,__ATOMIC_RELEASE,__ATOMIC_RELAXED);
        Call(GTOS_SYS_EXIT,line); for (;;) Yield();
    }
}
#define NEED(x) Need((x),__LINE__)
GtosProcessInfoResult Info() {
    GtosProcessInfoResult result={}; GtosProcessInfoRequest request={1,0,(unsigned)&result,sizeof(result)};
    NEED(Request(GTOS_SYS_PROCESS_INFO,request)==0 && result.version==1 && result.process_id
        && result.thread_id && result.scheduler_cpu_count==1);
    return result;
}
unsigned Own() {
    const unsigned tid=Info().thread_id;
    if (tid==Load(native_crypto_thread_record.parent)) return 3;
    for (unsigned i=0;i<3;++i) if (tid==Load(native_crypto_thread_record.worker[i].id)) return i;
    NEED(false); return 0;
}
void Empty() { for (unsigned i=0;i<5;++i) NEED(crypto_probe_get(i)==nullptr); }
void HeapEmpty() { GtosVmRegionInfo result={}; NEED(gtos_native_heap_query(&result)==0); }
void Await(const volatile unsigned& value,unsigned expected) { while (Load(value)!=expected) Yield(); }
void WaitQueue(unsigned count) {
    for (;;) {
        CryptoLockSnapshot value={}; crypto_probe_snapshot(&value);
        Store(native_crypto_thread_record.snapshotNext,value.next);
        Store(native_crypto_thread_record.snapshotServing,value.serving);
        Store(native_crypto_thread_record.snapshotReaders,value.readers);
        if (value.next-value.serving==count) return;
        Yield();
    }
}
struct Value { unsigned magic,owner,slot,wave,late,kind; };
Value* MakeValue(unsigned slot,unsigned late=0,unsigned kind=0) {
    auto* value=(Value*)malloc(sizeof(Value)); NEED(value!=nullptr);
    const unsigned owner=Own();
    *value={0xb551cafeU,owner,slot,owner==3 ? 0U : Load(native_crypto_thread_record.wave),late,kind};
    return value;
}
template <unsigned Slot> void Destroy(void* pointer);
void (*const destroyers[5])(void*)={Destroy<0>,Destroy<1>,Destroy<2>,Destroy<3>,Destroy<4>};
template <unsigned Slot> void Destroy(void* pointer) {
    const unsigned owner=Own(); auto& record=native_crypto_thread_record.worker[owner];
    if (!pointer) {
        NEED(Slot!=0); Empty(); Add(record.nullDestroyed); Yield(); return;
    }
    const Value value=*(const Value*)pointer;
    NEED(value.magic==0xb551cafeU && value.owner==owner && value.slot==Slot);
    if (value.kind) {
        NEED(owner==3 && Slot==4 && value.kind<=2); Empty();
        if (value.kind==1) Add(native_crypto_thread_record.failedDestructors);
        else Add(native_crypto_thread_record.successfulRetry);
        free(pointer); return;
    }
    NEED(value.wave==(owner==3 ? 0U : Load(native_crypto_thread_record.wave)));
    Add(record.destroyed); free(pointer);
    if (Slot==0) {
        Empty();
        if (!value.late) {
            NEED(!Load(record.reentered)); Store(record.reentered,1);
            Value* late=MakeValue(0,1); NEED(crypto_probe_set(0,late,destroyers[0])==1);
            NEED(crypto_probe_get(0)==late);
        } else NEED(Load(record.reentered)==1);
    } else if (value.late==0) {
        // The old snapshot callbacks must not erase the newly published slot0.
        NEED(crypto_probe_get(0)!=nullptr && crypto_probe_get(Slot)==nullptr);
    }
    Yield();
}
void RegistrationFailure() {
    Empty(); HeapEmpty();
    for (unsigned repeat=0;repeat<32;++repeat) Empty();
    HeapEmpty();
    Value* value=MakeValue(4,0,1);
    static_assert(GTOS_NATIVE_HEAP_BYTES==65536U,"bounded one-arena OOM fixture");
    void* fillers[256]; unsigned used=0; const unsigned sizes[]={256,1};
    for (unsigned size : sizes) {
        for (;;) {
            void* pointer=malloc(size);
            if (!pointer) break;
            NEED(used<256); fillers[used++]=pointer;
        }
    }
    NEED(used>100 && crypto_probe_set(4,value,destroyers[4])==0);
    Add(native_crypto_thread_record.failedRegistrations);
    NEED(Load(native_crypto_thread_record.failedDestructors)==1 && crypto_probe_get(4)==nullptr);
    while (used) free(fillers[--used]);
    HeapEmpty();
    Value* retry=MakeValue(4,0,2); NEED(crypto_probe_set(4,retry,destroyers[4])==1);
    NEED(crypto_probe_get(4)==retry); gtos_native_thread_cleanup();
    NEED(Load(native_crypto_thread_record.successfulRetry)==1); Empty(); HeapEmpty();
}
void SetSlots() {
    Empty();
    for (unsigned slot=0;slot<5;++slot) {
        Value* value=MakeValue(slot); NEED(crypto_probe_set(slot,value,destroyers[slot])==1);
        NEED(crypto_probe_get(slot)==value);
    }
}
void CheckSlots() {
    const unsigned owner=Own();
    for (unsigned slot=0;slot<5;++slot) {
        const auto* value=(const Value*)crypto_probe_get(slot);
        NEED(value && value->magic==0xb551cafeU && value->owner==owner && value->slot==slot
            && !value->late && !value->kind && value->wave==(owner==3 ? 0U : Load(native_crypto_thread_record.wave)));
    }
    Add(native_crypto_thread_record.worker[owner].tlsRounds);
}
void InitializeOnce() {
    Add(native_crypto_thread_record.onceEntries);
    Store(native_crypto_thread_record.onceFirst,0x1234abcd);
    while (!Load(native_crypto_thread_record.onceRelease)) Yield();
    Store(native_crypto_thread_record.onceSecond,0x55aa33cc);
}
void OnceCall() {
    crypto_probe_once(InitializeOnce);
    NEED(Load(native_crypto_thread_record.onceEntries)==1
        && Load(native_crypto_thread_record.onceFirst)==0x1234abcd
        && Load(native_crypto_thread_record.onceSecond)==0x55aa33cc);
    Add(native_crypto_thread_record.onceCalls);
}
void ExhaustTls() {
    static thread_local unsigned char large[65536];
    auto* volatile pointer=&large[0]; *pointer=1; NEED(false);
}
class ActualThread final : public Thread {
public:
    ActualThread(unsigned index,unsigned role) : Thread(Options("gtos-crypto-probe",8192)), index_(index),role_(role) {}
    void Run() override {
        asm volatile("pxor %%xmm0,%%xmm0" : : : "xmm0");
        const auto info=Info(); NEED(info.thread_id!=info.process_id && info.maximum_parallel_threads==4);
        auto& record=native_crypto_thread_record.worker[index_]; Store(record.id,info.thread_id);
        NEED(Own()==index_); Store(record.stage,1);
        if (role_==0) {
            crypto_probe_lock(0); Store(record.stage,2);
            Await(record.recurse,1); crypto_probe_lock(0); crypto_probe_lock(1); Store(record.recursed,1);
            Await(record.release,1); crypto_probe_lock(1);
        } else if (role_==1) {
            crypto_probe_lock(2); Store(record.order,Add(native_crypto_thread_record.sequence)); Store(record.stage,2);
            Await(record.release,1); crypto_probe_lock(3);
        } else if (role_==2) {
            crypto_probe_lock(0); Store(record.order,Add(native_crypto_thread_record.sequence)); Store(record.stage,2);
            Await(record.release,1); crypto_probe_lock(1);
        } else if (role_==3) {
            crypto_probe_lock(2); Store(record.order,Add(native_crypto_thread_record.sequence)); crypto_probe_lock(3);
        } else if (role_==4) {
            OnceCall();
        } else if (role_==5) {
            SetSlots(); Store(record.stage,2); Await(native_crypto_thread_record.go,1);
            for (unsigned i=0;i<16;++i) { Yield(); CheckSlots(); }
            Store(record.stage,3);
            Await(native_crypto_thread_record.go,2);
            const unsigned mode=Load(native_crypto_thread_record.mode);
            if (mode) {
                if (index_==0 && mode==14) ExhaustTls();
                if (index_==0 && mode==15) *(volatile unsigned*)0x70000000=1;
                for (;;) Yield();
            }
        } else if (role_==6) {
            crypto_probe_lock(Load(native_crypto_thread_record.mode)==9 ? 1 : 3); NEED(false);
        } else NEED(false);
        Store(record.stage,4);
    }
private:
    unsigned index_,role_;
};
void ResetWorkers() {
    for (unsigned i=0;i<3;++i) {
        volatile unsigned* words=(volatile unsigned*)&native_crypto_thread_record.worker[i];
        for (unsigned j=0;j<sizeof(CryptoThreadWorker)/4;++j) Store(words[j],0);
    }
}
void Start(ActualThread& thread,unsigned index) { NEED(thread.StartSynchronously()); while (!Load(native_crypto_thread_record.worker[index].stage)) Yield(); }
void Join(ActualThread& thread,unsigned index) { thread.Join(); NEED(Load(native_crypto_thread_record.worker[index].stage)==4); Add(native_crypto_thread_record.joins); }
void ReaderWriterCase() {
    ResetWorkers(); Store(native_crypto_thread_record.scenario,1); Store(native_crypto_thread_record.sequence,0);
    crypto_probe_lock(0);
    ActualThread reader(0,0),writer(1,1),newReader(2,2);
    Start(reader,0); Await(native_crypto_thread_record.worker[0].stage,2);
    CryptoLockSnapshot snapshot={}; crypto_probe_snapshot(&snapshot); NEED(snapshot.readers==2 && snapshot.ownDepth==1);
    Start(writer,1); WaitQueue(1);
    Start(newReader,2);
    for (unsigned i=0;i<32;++i) { Yield(); NEED(Load(native_crypto_thread_record.worker[1].stage)==1 && Load(native_crypto_thread_record.worker[2].stage)==1); }
    crypto_probe_lock(0); crypto_probe_snapshot(&snapshot); NEED(snapshot.ownDepth==2); crypto_probe_lock(1);
    Store(native_crypto_thread_record.worker[0].recurse,1); Await(native_crypto_thread_record.worker[0].recursed,1);
    Store(native_crypto_thread_record.worker[0].release,1); Join(reader,0);
    NEED(Load(native_crypto_thread_record.worker[1].stage)==1); crypto_probe_lock(1);
    Await(native_crypto_thread_record.worker[1].stage,2); NEED(Load(native_crypto_thread_record.worker[2].stage)==1);
    Store(native_crypto_thread_record.worker[1].release,1); Join(writer,1);
    Await(native_crypto_thread_record.worker[2].stage,2); Store(native_crypto_thread_record.worker[2].release,1); Join(newReader,2);
    NEED(Load(native_crypto_thread_record.worker[1].order)==1 && Load(native_crypto_thread_record.worker[2].order)==2);
    Add(native_crypto_thread_record.lockScenarios);
}
void WriterWrapCase() {
    ResetWorkers(); Store(native_crypto_thread_record.scenario,2); Store(native_crypto_thread_record.sequence,0);
    crypto_probe_seed(0xfffffffeU); crypto_probe_lock(2);
    ActualThread first(0,3),second(1,3),third(2,3); ActualThread* threads[]={&first,&second,&third};
    for (unsigned i=0;i<3;++i) { Start(*threads[i],i); WaitQueue(i+2); }
    CryptoLockSnapshot peak={}; crypto_probe_snapshot(&peak);
    NEED(peak.writer==Info().thread_id && peak.next-peak.serving==4 && !peak.readers);
    crypto_probe_lock(3);
    for (unsigned i=0;i<3;++i) { Join(*threads[i],i); NEED(Load(native_crypto_thread_record.worker[i].order)==i+1); }
    CryptoLockSnapshot snapshot={}; crypto_probe_snapshot(&snapshot);
    NEED(snapshot.next==2 && snapshot.serving==2 && !snapshot.writer && !snapshot.readers);
    Add(native_crypto_thread_record.lockScenarios);
}
void OnceCase() {
    ResetWorkers(); Store(native_crypto_thread_record.scenario,3);
    NEED(crypto_probe_nested_once()==1);
    ActualThread first(0,4),second(1,4),third(2,4);
    Start(first,0); Await(native_crypto_thread_record.onceEntries,1);
    Start(second,1); Start(third,2);
    for (unsigned i=0;i<32;++i) {
        Yield(); NEED(Load(native_crypto_thread_record.onceEntries)==1 && !Load(native_crypto_thread_record.onceSecond)
            && Load(native_crypto_thread_record.worker[1].stage)==1 && Load(native_crypto_thread_record.worker[2].stage)==1);
    }
    Store(native_crypto_thread_record.onceRelease,1); OnceCall();
    Join(first,0); Join(second,1); Join(third,2); NEED(Load(native_crypto_thread_record.onceCalls)==4);
}
void TlsCases() {
    Store(native_crypto_thread_record.scenario,4); SetSlots();
    const unsigned mode=Load(native_crypto_thread_record.mode);
    for (unsigned wave=1;wave<=(mode ? 1U : CRYPTO_THREAD_WAVES);++wave) {
        Store(native_crypto_thread_record.wave,wave); Store(native_crypto_thread_record.go,0); ResetWorkers();
        ActualThread first(0,5),second(1,5),third(2,5); ActualThread* threads[]={&first,&second,&third};
        for (unsigned i=0;i<3;++i) { Start(*threads[i],i); Await(native_crypto_thread_record.worker[i].stage,2); }
        Store(native_crypto_thread_record.go,1);
        for (unsigned i=0;i<16;++i) { Yield(); CheckSlots(); }
        for (unsigned i=0;i<3;++i) Await(native_crypto_thread_record.worker[i].stage,3);
        if (mode) {
            Store(native_crypto_thread_record.stage,10);
            if (mode!=13) Store(native_crypto_thread_record.go,2);
            for (;;) Yield();
        }
        Store(native_crypto_thread_record.go,2);
        for (unsigned i=0;i<3;++i) {
            Join(*threads[i],i); const auto& record=native_crypto_thread_record.worker[i];
            NEED(Load(record.destroyed)==6 && Load(record.nullDestroyed)==4 && Load(record.reentered)==1 && Load(record.tlsRounds)==16);
        }
        CheckSlots();
    }
}
void Main() {
    asm volatile("pxor %%xmm0,%%xmm0" : : : "xmm0");
    const auto info=Info(); NEED(info.thread_id==info.process_id); Store(native_crypto_thread_record.parent,info.process_id);
    const unsigned mode=Load(native_crypto_thread_record.mode);
    if (mode && mode<=12) {
        Store(native_crypto_thread_record.stage,10);
        if (mode==9 || mode==10) {
            crypto_probe_lock(mode==9 ? 0 : 2); ActualThread offender(0,6); Start(offender,0); for (;;) Yield();
        }
        crypto_probe_bad(mode); NEED(false);
    }
    if (!mode) RegistrationFailure();
    if (info.maximum_parallel_threads==1) {
        NEED(!mode); crypto_probe_lock(0); crypto_probe_lock(0); crypto_probe_lock(1); crypto_probe_lock(1);
        crypto_probe_lock(2); crypto_probe_lock(3); NEED(crypto_probe_nested_once()==1);
        SetSlots(); CheckSlots();
    } else {
        NEED(info.maximum_parallel_threads==4);
        if (!mode) { ReaderWriterCase(); WriterWrapCase(); OnceCase(); }
        TlsCases();
        NEED(Load(native_crypto_thread_record.joins)==33 && Load(native_crypto_thread_record.lockScenarios)==2);
    }
    gtos_native_thread_cleanup(); Empty(); HeapEmpty();
    const auto& root=native_crypto_thread_record.worker[3];
    NEED(Load(root.destroyed)==6 && Load(root.nullDestroyed)==4 && Load(root.reentered)==1);
    Store(native_crypto_thread_record.emptyHeap,1); Store(native_crypto_thread_record.stage,20);
    Call(GTOS_SYS_EXIT,0); for (;;) Yield();
}
}
extern "C" unsigned crypto_probe_current_tid() { return Info().thread_id; }
extern "C" void crypto_probe_yield() { Yield(); }
extern "C" __attribute__((section(".text.entry"),noreturn)) void _start() { Main(); for (;;) Yield(); }
