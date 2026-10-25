#include <hardwarecommunication/cpu_work_pool.h>

#ifdef GTOS_CPU_WORK_POOL_HOST_TEST
// Host-only seam. Every symbol below answers a question a real machine answers; nothing here decides admission,
// draining, parking or queue behaviour.
extern "C" uint32_t gtos_host_flags;
extern "C" uint32_t gtos_host_cr0;
extern "C" uint32_t gtos_host_cr3;
extern "C" uint32_t gtos_host_identity;
extern "C" uint32_t gtos_host_wake_result;
extern "C" uint32_t gtos_host_wake_calls;
#endif
#include <hardwarecommunication/cpu_memory_types.h>
#include <memorymanagement.h>
using namespace gtos;
using namespace gtos::hardwarecommunication;
extern "C" {
    extern uint32_t gtos_work_exception_table[32];
    extern uint8_t kernel_readonly_start, kernel_readonly_end;
    void gtos_work_wake_interrupt();
    void gtos_work_spurious_interrupt();
    void gtos_work_unexpected_interrupt();
}
namespace {
    struct Gate { uint16_t low, selector; uint8_t reserved, access; uint16_t high; } __attribute__((packed));
    struct Idtr { uint16_t limit; uint32_t base; } __attribute__((packed));
    struct InterruptFrame { uint32_t edi,esi,ebp,esp,ebx,edx,ecx,eax,vector,error,eip,cs,eflags; };
    struct Worker {
        CpuWorkQueue queue;
        uint32_t apicId, stackBase, stackPages;
        volatile uint32_t state, failure, observedCr3, observedCr0, wakes, spurious;
        volatile uint32_t faultVector, faultError, faultInstruction, faultAddress;
#ifdef GTOS_CPU_WORK_POOL_TEST
        volatile uint32_t injectedFault, testHold, testHoldAck;
#endif
        Idtr idtr;
        Gate idt[256];
    } __attribute__((aligned(64)));
#ifdef GTOS_CPU_WORK_POOL_HOST_TEST
    uint32_t Flags() { return gtos_host_flags; }
    uint32_t Cr0() { return gtos_host_cr0; }
    uint32_t Cr3() { return gtos_host_cr3; }
    uint32_t Identity() { return gtos_host_identity; }
#else
    uint32_t Flags() { uint32_t value; asm volatile("pushfl; popl %0":"=r"(value)); return value; }
    uint32_t Cr0() { uint32_t value; asm volatile("movl %%cr0,%0":"=r"(value)); return value; }
    uint32_t Cr3() { uint32_t value; asm volatile("movl %%cr3,%0":"=r"(value)); return value; }
    uint32_t Identity() {
        uint32_t a,b,c,d;
        asm volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(1),"c"(0)); return b>>24;
    }
#endif
    uint32_t Load(const volatile uint32_t& value) { return __atomic_load_n(&value,__ATOMIC_RELAXED); }
    void Store(volatile uint32_t& target,uint32_t value) { __atomic_store_n(&target,value,__ATOMIC_RELAXED); }
    uint32_t State(const Worker& worker) { return __atomic_load_n(&worker.state,__ATOMIC_ACQUIRE); }
    void SetState(Worker& worker,CpuWorkerState state) { __atomic_store_n(&worker.state,(uint32_t)state,__ATOMIC_RELEASE); }
    bool Ready(uint32_t state) { return state==CpuWorkerIdle||state==CpuWorkerBusy; }
#ifdef GTOS_CPU_WORK_POOL_HOST_TEST
    // Interrupt masking is not observable on the host; the seam records the intent without executing it.
    class Guard {
        uint32_t flags;
    public:
        Guard():flags(Flags()) {}
        ~Guard() {}
    };
#else
    class Guard {
        uint32_t flags;
    public:
        Guard():flags(Flags()) { asm volatile("cli":::"memory"); }
        ~Guard() { if(flags&0x200) asm volatile("sti":::"memory"); }
    };
#endif
    void Zero(void* pointer,uint32_t size) { uint8_t* p=(uint8_t*)pointer; for(uint32_t i=0;i<size;++i)p[i]=0; }
    void SetGate(Gate& gate,uint32_t entry) {
        gate.low=entry;gate.high=entry>>16;gate.selector=8;gate.reserved=0;gate.access=0x8E;
    }
    bool Mapped(memory::KernelPaging& paging,uint32_t address,uint32_t bytes,bool writable,const CpuMemoryTypes* types,bool device=false) {
#ifdef GTOS_CPU_WORK_POOL_HOST_TEST
        // The host has no page tables; the seam reports the mapping as present so Start()'s paging gate is not the
        // thing under test here.
        (void)paging; (void)address; (void)writable; (void)types; (void)device;
        return bytes != 0;
#else
        if(!bytes||(uint64_t)address+bytes>0x100000000ULL)return false;
        if (types && !device && !MemoryTypeRangeIsWriteBack(*types,address,bytes)) return false;
        uint32_t start=address&~4095U;
        uint64_t end=((uint64_t)address+bytes+4095)&~4095ULL;
        for(uint64_t current=start;current<end;current+=4096){
            memory::PagingMapping map;
            if(!paging.query((uint32_t)current,map)||map.physicalAddress!=current||map.userAccessible
                ||(writable!=map.writable)||(device&&!map.cacheDisabled))return false;
        }
        return true;
#endif
    }
#ifdef GTOS_CPU_WORK_POOL_HOST_TEST
    bool WaitIcr(volatile uint32_t*) { return true; }
#else
    bool WaitIcr(volatile uint32_t* lapic) {
        for(uint32_t i=0;i<100000;++i){if(!(lapic[0x300/4]&(1U<<12)))return true;asm volatile("pause");}
        return false;
    }
#endif
}
namespace gtos { namespace hardwarecommunication {
    struct CpuWorkPoolShared {
        uint32_t bspApicId, localApic, directory, count, allocationBase, allocationPages;
        CpuMemoryTypes memoryTypes;
        Worker* byApic[256];
        Worker* workers[256];
    };
} }
namespace {
    CpuWorkPoolShared* activePool=0;
    Worker* CurrentWorker() {
        if(!activePool)return 0;
        uint32_t id=Identity();return id<256?activePool->byApic[id]:0;
    }
    bool SendWake(CpuWorkPoolShared* pool,uint32_t apicId) {
#ifdef GTOS_CPU_WORK_POOL_HOST_TEST
        (void)pool; (void)apicId;
        ++gtos_host_wake_calls;
        return gtos_host_wake_result != 0;
#else
        if(!pool||apicId>=255)return false;
        volatile uint32_t* lapic=(volatile uint32_t*)pool->localApic;
        if(!WaitIcr(lapic))return false;
        lapic[0x310/4]=apicId<<24;lapic[0x300/4]=0x40F0; // physical fixed IPI, assert.
        (void)lapic[0x20/4];return WaitIcr(lapic);
#endif
    }
}
extern "C" uint32_t gtos_work_ap_interrupt(uint32_t frameAddress) {
    const InterruptFrame& frame=*(const InterruptFrame*)frameAddress;
    Worker* worker=CurrentWorker();
    if(worker&&frame.vector==0xF0){
        __atomic_fetch_add(&worker->wakes,1U,__ATOMIC_RELAXED);
        ((volatile uint32_t*)activePool->localApic)[0xB0/4]=0;
    }else if(worker&&frame.vector==0xFF)__atomic_fetch_add(&worker->spurious,1U,__ATOMIC_RELAXED);
    return frameAddress;
}
extern "C" void gtos_work_ap_fault(uint32_t frameAddress) {
    const InterruptFrame& frame=*(const InterruptFrame*)frameAddress;
    Worker* worker=CurrentWorker();
    if (worker) {
        uint32_t address=0;
        if (frame.vector==14) asm volatile("movl %%cr2,%0" : "=r"(address));
        Store(worker->failure,CpuWorkerException);
        Store(worker->faultVector,frame.vector); Store(worker->faultError,frame.error);
        Store(worker->faultInstruction,frame.eip); Store(worker->faultAddress,address);
        SetState(*worker,CpuWorkerFaulted);
    }
    for(;;)asm volatile("cli; hlt":::"memory");
}
CpuWorkTicket::CpuWorkTicket():workerIndex(0xFFFFFFFFU),ticket(){}
CpuWorkPool::CpuWorkPool():shared(0),startup(0),error(CpuWorkPoolOk),prepared(false),started(false),closed(false){}
bool CpuWorkPool::OnBootstrapProcessor()const{return shared&&(!shared->count||Identity()==shared->bspApicId);}
int CpuWorkPool::FindWorker(uint32_t apicId)const{
    if (!shared) return -1;
    for (uint32_t i=0;i<shared->count;++i)
        if (shared->workers[i]->apicId==apicId) return i;
    return -1;
}
bool CpuWorkPool::Prepare(CpuStartup& boot,memory::PhysicalMemoryManager& frames){
    if(prepared||activePool||(Flags()&0x200)||(Cr0()&0x80000000U)){error=CpuWorkPoolBadState;return false;}
    CpuStartupReport report=boot.GetReport();
    if(!boot.ReadyToStart()||report.error!=CpuStartupOk||!report.detectedProcessors||report.attemptedAps){error=CpuWorkPoolBadState;return false;}
    uint32_t candidates=0;
    for(uint32_t i=0;i<report.detectedProcessors;++i){CpuStartupProcessorInfo cpu;if(boot.GetProcessor(i,cpu)&&cpu.stackPages)++candidates;}
    CpuMemoryTypes memoryTypes;
    if (candidates && !CaptureMemoryTypes(memoryTypes)) { error=CpuWorkPoolBadMemoryTypes; return false; }
    uint32_t bytes=sizeof(CpuWorkPoolShared)+63+candidates*sizeof(Worker),pages=(bytes+4095)/4096,address=0;
    if(!frames.allocateContiguous(pages,address,1,0xFFFFEFFFU)){error=CpuWorkPoolNoMemory;return false;}
    shared=(CpuWorkPoolShared*)address;Zero(shared,pages*4096);
    shared->bspApicId=report.detectedProcessors>1?Identity():0;shared->localApic=report.localApicAddress;
    shared->allocationBase=address;shared->allocationPages=pages;
    if (candidates) shared->memoryTypes=memoryTypes;
    uint32_t next=((address+sizeof(CpuWorkPoolShared)+63)&~63U);
    for(uint32_t i=0;i<report.detectedProcessors;++i){
        CpuStartupProcessorInfo cpu;if(!boot.GetProcessor(i,cpu)||!cpu.stackPages)continue;
        if(cpu.apicId>=255||cpu.apicId==shared->bspApicId)continue;
        Worker* worker=new((void*)next) Worker;next+=sizeof(Worker);
        worker->apicId=cpu.apicId;worker->stackBase=cpu.stackBase;worker->stackPages=cpu.stackPages;
        worker->idtr.limit=sizeof(worker->idt)-1;worker->idtr.base=(uint32_t)worker->idt;
        for(uint32_t vector=0;vector<256;++vector)SetGate(worker->idt[vector],(uint32_t)&gtos_work_unexpected_interrupt);
        for(uint32_t vector=0;vector<32;++vector)SetGate(worker->idt[vector],gtos_work_exception_table[vector]);
        SetGate(worker->idt[0xF0],(uint32_t)&gtos_work_wake_interrupt);
        SetGate(worker->idt[0xFF],(uint32_t)&gtos_work_spurious_interrupt);
        Store(worker->faultVector,0xFFFFFFFFU);SetState(*worker,CpuWorkerPrepared);
        shared->workers[shared->count++]=worker;shared->byApic[cpu.apicId]=worker;
    }
    startup=&boot;prepared=true;error=CpuWorkPoolOk;return true;
}
void CpuWorkPool::ApplicationProcessorMain(uint32_t apicId,void* opaque){
    CpuWorkPoolShared* pool=(CpuWorkPoolShared*)opaque;
    Worker* worker=apicId<256?pool->byApic[apicId]:0;
    if(!worker||Identity()!=apicId)for(;;)asm volatile("cli; hlt");
    SetState(*worker,CpuWorkerStarting);
    asm volatile("lidt %0"::"m"(worker->idtr):"memory");
    uint32_t featureA,featureB,featureC,featureD;
    asm volatile("cpuid" : "=a"(featureA),"=b"(featureB),"=c"(featureC),"=d"(featureD) : "a"(1),"c"(0));
    if ((featureD & ((1U<<5)|(1U<<9))) != ((1U<<5)|(1U<<9))) {
        Store(worker->failure,CpuWorkerApicMismatch); SetState(*worker,CpuWorkerFaulted);
        for (;;) asm volatile("cli; hlt");
    }
    uint32_t apicLow,apicHigh;
    asm volatile("rdmsr" : "=a"(apicLow),"=d"(apicHigh) : "c"(0x1B));
    if (apicHigh || (apicLow & 0xD00U) != 0x800U || (apicLow & 0xFFFFF000U) != pool->localApic) {
        Store(worker->failure,CpuWorkerApicMismatch); SetState(*worker,CpuWorkerFaulted);
        for (;;) asm volatile("cli; hlt");
    }
    if (!PrepareApplicationProcessorMemory(pool->memoryTypes)) {
        Store(worker->failure,CpuWorkerMemoryTypeMismatch); SetState(*worker,CpuWorkerFaulted);
        for (;;) asm volatile("cli; hlt");
    }
    asm volatile("movl %0,%%cr3"::"r"(pool->directory):"memory");
    uint32_t control=Cr0()|0x80010000U;asm volatile("movl %0,%%cr0"::"r"(control):"memory");
    Store(worker->observedCr3,Cr3());Store(worker->observedCr0,Cr0());
    volatile uint32_t* lapic=(volatile uint32_t*)pool->localApic;
    if ((lapic[0x20/4] >> 24) != apicId) {
        Store(worker->failure,CpuWorkerApicMismatch); SetState(*worker,CpuWorkerFaulted);
        for (;;) asm volatile("cli; hlt");
    }
    // AP-local masks: the PIC/BSP interrupt routing is never modified here.
    lapic[0x320/4]=1U<<16;lapic[0x350/4]=1U<<16;lapic[0x360/4]=1U<<16;lapic[0x370/4]=1U<<16;
    uint32_t maxLvt=(lapic[0x30/4]>>16)&0xFF;
    if(maxLvt>=4)lapic[0x340/4]=1U<<16;
    if(maxLvt>=5)lapic[0x330/4]=1U<<16;
    lapic[0x80/4]=0;lapic[0xF0/4]=0x1FF;(void)lapic[0x20/4];
    if(Cr3()!=pool->directory||(Cr0()&0xE0010001U)!=0x80010001U){SetState(*worker,CpuWorkerFaulted);for(;;)asm volatile("cli; hlt");}
    for(;;){
        asm volatile("cli":::"memory");
#ifdef GTOS_CPU_WORK_POOL_TEST
        if (__atomic_load_n(&worker->testHold,__ATOMIC_ACQUIRE)) {
            SetState(*worker,CpuWorkerBusy);
            __atomic_store_n(&worker->testHoldAck,1U,__ATOMIC_RELEASE);
            asm volatile("sti" : : : "memory");
            while (__atomic_load_n(&worker->testHold,__ATOMIC_ACQUIRE)) asm volatile("pause");
            asm volatile("cli" : : : "memory");
            __atomic_store_n(&worker->testHoldAck,0U,__ATOMIC_RELEASE);
        }
        if (__atomic_load_n(&worker->injectedFault,__ATOMIC_ACQUIRE))
            asm volatile("movl $0x12345678,0" : : : "memory");
#endif
        // A parked worker is set by the BSP only after the pool reported drained, so the AP leaves its work loop and
        // halts with interrupts clear. It never touches a payload again.
        if(__atomic_load_n(&worker->state,__ATOMIC_ACQUIRE)==(uint32_t)CpuWorkerParked){
            SetState(*worker,CpuWorkerParked);
            for(;;)asm volatile("cli; hlt":::"memory");
        }
        if(!worker->queue.HasPending()){
            SetState(*worker,CpuWorkerIdle);
            asm volatile("sti; hlt":::"memory");
        }else{
            SetState(*worker,CpuWorkerBusy);
            asm volatile("sti":::"memory");
            worker->queue.ExecuteOne(Identity());
        }
    }
}
bool CpuWorkPool::Start(memory::KernelPaging& paging){
    if(!prepared||started||activePool||!OnBootstrapProcessor()||(Flags()&0x200)||(Cr0()&0x80000000U)){error=CpuWorkPoolBadState;return false;}
    memory::PagingStatistics stats=paging.getStatistics();
    const CpuMemoryTypes* types=shared->count?&shared->memoryTypes:0;
    if(!stats.prepared||!stats.directoryAddress){error=CpuWorkPoolBadPaging;return false;}
    uint32_t cr4; asm volatile("movl %%cr4,%0" : "=r"(cr4));
    if ((cr4 & (1U<<5)) || (uint32_t)&kernel_readonly_end <= (uint32_t)&kernel_readonly_start
        || !Mapped(paging,(uint32_t)&kernel_readonly_start,
                   (uint32_t)&kernel_readonly_end-(uint32_t)&kernel_readonly_start,false,types)) {
        error=CpuWorkPoolBadPaging; return false;
    }
    if(!Mapped(paging,shared->allocationBase,shared->allocationPages*4096,true,types)
        ||!Mapped(paging,(uint32_t)&activePool,sizeof(activePool),true,types)
        ||(shared->count&&!Mapped(paging,shared->localApic,4096,true,types,true))){error=CpuWorkPoolBadPaging;return false;}
    CpuStartupReport bootReport=startup->GetReport();
    if(shared->count&&!Mapped(paging,bootReport.trampolineAddress,4096,true,types)){error=CpuWorkPoolBadPaging;return false;}
    for(uint32_t i=0;i<shared->count;++i)if(!Mapped(paging,shared->workers[i]->stackBase,shared->workers[i]->stackPages*4096,true,types)){error=CpuWorkPoolBadPaging;return false;}
    if (shared->count) {
        uint32_t bootAddress,bootBytes;
        if (!startup->GetSharedMemoryRange(bootAddress,bootBytes)
            || !Mapped(paging,bootAddress,bootBytes,true,types)
            || !Mapped(paging,stats.directoryAddress,4096,true,types)) {
            error=CpuWorkPoolBadPaging; return false;
        }
        const uint32_t* directory=(const uint32_t*)stats.directoryAddress;
        for (uint32_t index=0;index<1024;++index) {
            uint32_t entry=directory[index];
            if ((entry&1) && ((entry&0x80) || !Mapped(paging,entry&0xFFFFF000U,4096,true,types))) {
                error=CpuWorkPoolBadPaging; return false;
            }
        }
    }
    if(shared->count && !paging.sealForSharedProcessors()){error=CpuWorkPoolBadPaging;return false;}
    error=CpuWorkPoolOk;shared->directory=stats.directoryAddress;activePool=shared;started=true;
    if(!startup->StartPrepared(&ApplicationProcessorMain,shared))error=CpuWorkPoolStartupFailed;
    for (uint32_t cpuIndex=0;cpuIndex<bootReport.detectedProcessors;++cpuIndex) {
        CpuStartupProcessorInfo info;
        if (!startup->GetProcessor(cpuIndex,info) || info.state!=CpuStartupSelfTestFailed) continue;
        int workerIndex=FindWorker(info.apicId);
        if (workerIndex>=0) {
            Worker& worker=*shared->workers[workerIndex];
            Store(worker.failure,CpuWorkerSelfTestMismatch);
            uint32_t expected=CpuWorkerPrepared;
            __atomic_compare_exchange_n(&worker.state,&expected,(uint32_t)CpuWorkerFaulted,
                                        false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
        }
    }
    for(uint32_t poll=0;poll<10000000U;++poll){
        uint32_t ready=0,faulted=0;
        for(uint32_t i=0;i<shared->count;++i){uint32_t state=State(*shared->workers[i]);if(Ready(state))++ready;if(state==CpuWorkerFaulted)++faulted;}
        if(ready+faulted==shared->count)break;
        asm volatile("pause");
    }
    CpuWorkPoolReport final=GetReport();
    if(error==CpuWorkPoolOk&&final.faultedWorkers)error=CpuWorkPoolWorkerSetupFailed;
    if(error==CpuWorkPoolOk&&final.readyWorkers!=shared->count)error=CpuWorkPoolReadyTimeout;
    return error==CpuWorkPoolOk;
}
CpuWorkSubmitStatus CpuWorkPool::Submit(uint32_t apicId,const WorkRequest& request,CpuWorkTicket& ticket){
    ticket=CpuWorkTicket();Guard guard;
    if(!OnBootstrapProcessor())return CpuWorkWrongContext;
    // Closure dominates every other admission rule: once admission is closed a new job is refused outright, and the
    // caller's ticket is left invalid so it cannot be mistaken for a queued job.
    if(closed)return CpuWorkOffline;
    int index=FindWorker(apicId);if(index<0||!started||!Ready(State(*shared->workers[index])))return CpuWorkOffline;
    WorkTicket local;WorkSubmitStatus status=shared->workers[index]->queue.Submit(request,local);
    if (status==WorkFull) return CpuWorkFull;
    if (status==WorkInvalid) return CpuWorkInvalid;
    if (status==WorkExhausted) return CpuWorkExhausted;
    ticket.workerIndex=index;ticket.ticket=local;
    if(!SendWake(shared,apicId)){error=CpuWorkPoolWakeFailed;return CpuWorkAcceptedWakeFailed;}
    return CpuWorkAccepted;
}
CpuWorkCollectStatus CpuWorkPool::Collect(const CpuWorkTicket& ticket,WorkResult& result){
    Guard guard;if(!OnBootstrapProcessor()||ticket.workerIndex>=shared->count)return CpuWorkTicketInvalid;
    Worker& worker=*shared->workers[ticket.workerIndex];WorkCollectStatus status=worker.queue.Collect(ticket.ticket,result);
    if (status==WorkComplete) return CpuWorkComplete;
    if (status==WorkTicketInvalid) return CpuWorkTicketInvalid;
    return State(worker)==CpuWorkerFaulted?CpuWorkFailed:CpuWorkPending;
}
bool CpuWorkPool::Kick(uint32_t apicId){Guard guard;int index=FindWorker(apicId);return OnBootstrapProcessor()&&index>=0&&!closed&&Ready(State(*shared->workers[index]))&&SendWake(shared,apicId);}
bool CpuWorkPool::CloseAdmission(){
    Guard guard;
    if(!OnBootstrapProcessor()){error=CpuWorkPoolBadState;return false;}
    // One-way and idempotent. Nothing in flight is disturbed: every issued ticket stays collectable and every
    // already-accepted job still runs to completion.
    closed=true;
    return true;
}
bool CpuWorkPool::AdmissionClosed()const{return closed;}
uint32_t CpuWorkPool::SubmittedJobs()const{
    if(!shared)return 0;
    uint32_t total=0;
    for(uint32_t i=0;i<shared->count;++i)total+=shared->workers[i]->queue.GetStats().submitted;
    return total;
}
bool CpuWorkPool::IsDrained(uint32_t& queued,uint32_t& busy,uint32_t& uncollected)const{
    queued=busy=uncollected=0;
    if(!shared)return false;
    for(uint32_t i=0;i<shared->count;++i){
        const Worker& worker=*shared->workers[i];
        const WorkQueueStats stats=worker.queue.GetStats();
        // submitted - completed is what the consumer has not finished; completed - collected is what the BSP has not
        // taken. Both must reach zero, and no worker may be mid-execution.
        if(stats.submitted>stats.completed)queued+=stats.submitted-stats.completed;
        if(stats.completed>stats.collected)uncollected+=stats.completed-stats.collected;
        if(State(worker)==CpuWorkerBusy)++busy;
    }
    return queued==0&&busy==0&&uncollected==0;
}
bool CpuWorkPool::ParkWorkers(){
    Guard guard;
    if(!OnBootstrapProcessor()){error=CpuWorkPoolBadState;return false;}
    if(!shared||!started){error=CpuWorkPoolBadState;return false;}
    // Parking an AP that still holds work would abandon that work, so the pool refuses rather than pretending.
    uint32_t queued=0,busy=0,uncollected=0;
    if(!IsDrained(queued,busy,uncollected)){error=CpuWorkPoolBadState;return false;}
    for(uint32_t i=0;i<shared->count;++i){
        Worker& worker=*shared->workers[i];
        if(State(worker)==CpuWorkerFaulted)continue;
        SetState(worker,CpuWorkerParked);
    }
    return true;
}
CpuWorkPoolReport CpuWorkPool::GetReport()const{
    CpuWorkPoolReport result={};result.error=error;result.schedulerOnlineProcessors=1;if(!shared)return result;
    result.configuredWorkers=shared->count;result.sharedPageDirectory=shared->directory;
    for(uint32_t i=0;i<shared->count;++i){const Worker& worker=*shared->workers[i];uint32_t state=State(worker);
        if(Ready(state)&&Load(worker.observedCr3)==shared->directory&&(Load(worker.observedCr0)&0xE0010001U)==0x80010001U)++result.readyWorkers;
        if (state==CpuWorkerBusy) ++result.busyWorkers;
        if (state==CpuWorkerFaulted) ++result.faultedWorkers;
        result.completedJobs+=worker.queue.GetStats().completed;
    }return result;
}
bool CpuWorkPool::GetWorker(uint32_t index,CpuWorkerSnapshot& result)const{
    if (!shared||index>=shared->count) return false;
    const Worker& worker=*shared->workers[index];
    result.apicId=worker.apicId;result.state=(CpuWorkerState)State(worker);
    result.failure=(CpuWorkerFailure)Load(worker.failure);
    result.observedCr3=Load(worker.observedCr3);result.observedCr0=Load(worker.observedCr0);
    result.wakeInterrupts=Load(worker.wakes);result.spuriousInterrupts=Load(worker.spurious);
    result.faultVector=Load(worker.faultVector);result.faultError=Load(worker.faultError);
    result.faultInstruction=Load(worker.faultInstruction);result.faultAddress=Load(worker.faultAddress);
    result.queue=worker.queue.GetStats();return true;
}
const char* CpuWorkPool::ErrorName(CpuWorkPoolError error){switch(error){
    case CpuWorkPoolOk:return "kernel worker pool ready";case CpuWorkPoolBadState:return "invalid worker boot state";
    case CpuWorkPoolWrongCpu:return "worker control requires BSP";case CpuWorkPoolNoMemory:return "no worker context memory";
    case CpuWorkPoolBadPaging:return "worker identity mappings or sealed CR3 invalid";
    case CpuWorkPoolStartupFailed:return "AP handoff failed";case CpuWorkPoolReadyTimeout:return "AP worker readiness timed out";
    case CpuWorkPoolBadMemoryTypes:return "BSP memory-type snapshot unavailable";
    case CpuWorkPoolWorkerSetupFailed:return "AP worker compatibility/setup failed";
    case CpuWorkPoolWakeFailed:return "job queued but wake IPI failed";}return "unknown worker error";}

#ifdef GTOS_CPU_WORK_POOL_TEST
extern "C" bool GtosWorkHoldForTest(uint32_t apicId) {
    Guard guard;
    if (!activePool || Identity()!=activePool->bspApicId || apicId>=255
        || !activePool->byApic[apicId] || !Ready(State(*activePool->byApic[apicId]))) return false;
    __atomic_store_n(&activePool->byApic[apicId]->testHold,1U,__ATOMIC_RELEASE);
    return SendWake(activePool,apicId);
}
extern "C" bool GtosWorkHeldForTest(uint32_t apicId) {
    return activePool && apicId<255 && activePool->byApic[apicId]
        && __atomic_load_n(&activePool->byApic[apicId]->testHoldAck,__ATOMIC_ACQUIRE);
}
extern "C" bool GtosWorkInjectNullFault(uint32_t apicId) {
    Guard guard;
    if (!activePool || Identity()!=activePool->bspApicId || apicId>=255
        || !activePool->byApic[apicId] || !Ready(State(*activePool->byApic[apicId]))) return false;
    __atomic_store_n(&activePool->byApic[apicId]->injectedFault,1U,__ATOMIC_RELEASE);
    __atomic_store_n(&activePool->byApic[apicId]->testHold,0U,__ATOMIC_RELEASE);
    return SendWake(activePool,apicId);
}
#endif

#ifdef GTOS_CPU_WORK_POOL_HOST_TEST
namespace {
    // A static shared block for the host unit, so no allocator is involved. The worker records it holds contain real
    // CpuWorkQueue instances, which is the point: the queue is never simulated.
    CpuWorkPoolShared* hostShared = 0;
    uint8_t hostStorage[sizeof(CpuWorkPoolShared) + 63 + 4 * sizeof(Worker)];
}
extern "C" bool gtos_host_install_pool(uint32_t workerCount, uint32_t bspApicId, uint32_t lapicAddress) {
    if (hostShared) return false;
    if (workerCount > 4) return false;
    hostShared = (CpuWorkPoolShared*)hostStorage;
    Zero(hostShared, sizeof(hostStorage));
    hostShared->bspApicId = bspApicId;
    hostShared->localApic = lapicAddress;
    hostShared->allocationBase = (uint32_t)hostStorage;
    hostShared->allocationPages = (sizeof(hostStorage) + 4095) / 4096;
    uint32_t next = ((uint32_t)hostStorage + sizeof(CpuWorkPoolShared) + 63) & ~63U;
    for (uint32_t i = 0; i < workerCount; ++i) {
        Worker* worker = new ((void*)next) Worker;
        next += sizeof(Worker);
        // Each worker gets a distinct APIC id above the BSP, so FindWorker and the identity checks are meaningful.
        worker->apicId = bspApicId + 1 + i;
        worker->stackBase = 0x10000 + i * 0x4000;
        worker->stackPages = 4;
        // A worker counts as ready only after it published its own CR3 and CR0, which a real AP does in its entry. The
        // seam installs exactly those observations, because a started worker that had not published them would not be
        // ready in production either.
        hostShared->directory = 0x5000U;
        Store(worker->observedCr3, hostShared->directory);
        Store(worker->observedCr0, 0x80010011U);
        SetState(*worker, CpuWorkerIdle);
        hostShared->workers[hostShared->count++] = worker;
        hostShared->byApic[worker->apicId] = worker;
    }
    return true;
}
// The friend struct that carries the private-state access. The extern "C" entry point below calls through it, so the
// seam keeps C linkage while still reaching the pool's own members.
namespace gtos { namespace hardwarecommunication {
struct CpuWorkPoolHostTest {
    static bool Attach(void* poolAddress) {
        if (!hostShared || !poolAddress) return false;
        CpuWorkPool* pool = (CpuWorkPool*)poolAddress;
        pool->shared = hostShared;
        pool->prepared = true;
        pool->started = true;
        pool->error = CpuWorkPoolOk;
        activePool = hostShared;
        return true;
    }
};
} }
extern "C" bool gtos_host_attach_pool(void* poolAddress) {
    return gtos::hardwarecommunication::CpuWorkPoolHostTest::Attach(poolAddress);
}
// The real ExecuteOne on a real worker's queue: this is the AP side of the contract, driven without an AP.
extern "C" bool gtos_host_worker_execute(uint32_t workerIndex, uint32_t apicId) {
    if (!hostShared || workerIndex >= hostShared->count) return false;
    Worker* worker = hostShared->workers[workerIndex];
    SetState(*worker, CpuWorkerBusy);
    const bool ran = worker->queue.ExecuteOne(apicId);
    SetState(*worker, CpuWorkerIdle);
    return ran;
}
extern "C" uint32_t gtos_host_worker_state(uint32_t workerIndex) {
    if (!hostShared || workerIndex >= hostShared->count) return 0xFFFFFFFFU;
    return State(*hostShared->workers[workerIndex]);
}
extern "C" void gtos_host_reset_pool() {
    // Only the test may reset, and only between independent cases; the production pool never resets.
    hostShared = 0;
    activePool = 0;
}
#endif
