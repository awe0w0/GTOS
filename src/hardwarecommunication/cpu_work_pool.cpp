#include <hardwarecommunication/cpu_work_pool.h>

#ifdef GTOS_CPU_WORK_POOL_HOST_TEST
// Host-only seam. Every symbol below answers a question a real machine answers; nothing here decides admission,
// draining, parking or queue behaviour.
extern "C" uint32_t gtos_host_flags;
extern "C" uint32_t gtos_host_cr0;
extern "C" uint32_t gtos_host_cr3;
extern "C" uint32_t gtos_host_cr4;
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
    const uint32_t Cr4Pae = 1U << 5;
    struct Gate { uint16_t low, selector; uint8_t reserved, access; uint16_t high; } __attribute__((packed));
    struct Idtr { uint16_t limit; uint32_t base; } __attribute__((packed));
    struct InterruptFrame { uint32_t edi,esi,ebp,esp,ebx,edx,ecx,eax,vector,error,eip,cs,eflags; };
    struct Worker {
        CpuWorkQueue queue;
        uint32_t apicId, stackBase, stackPages;
        volatile uint32_t state, failure, observedCr3, observedCr0, observedCr4, wakes, spurious;
        volatile uint32_t faultVector, faultError, faultInstruction, faultAddress;
        CpuMemoryTypeFailure memoryTypeFailure;
        // Park protocol: the BSP writes the request, the AP writes the acknowledgement after its last job access.
        volatile uint32_t parkRequested, parkAckedSession, parkAckCount;
        // The queue identity a pool ticket carries, so a raw (slot, id) pair is addressable on one worker only.
        uint32_t queueTag;
#if defined(GTOS_CPU_WORK_POOL_TEST) || defined(GTOS_CPU_WORK_POOL_HOST_TEST)
        volatile uint32_t injectedFault, testHold, testHoldAck;
        // The real-step pause. Test-only; the step itself is production code.
        volatile uint32_t stepGate, stepReached;
#endif
        Idtr idtr;
        Gate idt[256];
    } __attribute__((aligned(64)));
#ifdef GTOS_CPU_WORK_POOL_HOST_TEST
    uint32_t Flags() { return gtos_host_flags; }
    uint32_t Cr0() { return gtos_host_cr0; }
    uint32_t Cr3() { return gtos_host_cr3; }
    // The control-register reads are privileged. The seam answers them on the host.
    uint32_t Cr4() { return gtos_host_cr4; }
    uint32_t Identity() { return gtos_host_identity; }
#else
    uint32_t Flags() { uint32_t value; asm volatile("pushfl; popl %0":"=r"(value)); return value; }
    uint32_t Cr0() { uint32_t value; asm volatile("movl %%cr0,%0":"=r"(value)); return value; }
    uint32_t Cr3() { uint32_t value; asm volatile("movl %%cr3,%0":"=r"(value)); return value; }
    uint32_t Cr4() { uint32_t value; asm volatile("movl %%cr4,%0":"=r"(value)); return value; }
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
    bool PagingReady(const Worker& worker, uint32_t directory, bool pae) {
        return Load(worker.observedCr3)==directory && (Load(worker.observedCr0)&0xE0010001U)==0x80010001U
            && ((Load(worker.observedCr4)&Cr4Pae)!=0)==pae;
    }
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
    bool MappedTables(memory::KernelPaging& paging, uint32_t root, bool pae, const CpuMemoryTypes* types) {
        if (!root || (root&4095U) || !Mapped(paging,root,4096,true,types)) return false;
        if (!pae) {
            const uint32_t* directory=(const uint32_t*)root;
            for (uint32_t i=0;i<1024;++i) {
                const uint32_t entry=directory[i];
                if ((entry&1) && ((entry&0x80) || !Mapped(paging,entry&0xFFFFF000U,4096,true,types)))
                    return false;
            }
            return true;
        }
        const uint64_t address=0xFFFFF000ULL, accessed=1U<<5;
        const uint64_t* pdpt=(const uint64_t*)root;
        for (uint32_t pd=0;pd<4;++pd) {
            const uint64_t entry=pdpt[pd];
            const uint32_t frame=(uint32_t)(entry&address);
            if (!(entry&1) || (entry&~(address|accessed|1)) || !frame || frame==root
                || !Mapped(paging,frame,4096,true,types)) return false;
            for (uint32_t other=0;other<pd;++other)
                if ((pdpt[other]&address)==frame) return false;
            const uint64_t* directory=(const uint64_t*)frame;
            for (uint32_t i=0;i<512;++i) {
                const uint64_t pde=directory[i];
                if (!pde) continue;
                const uint32_t table=(uint32_t)(pde&address);
                if ((pde&3)!=3 || (pde&~(address|accessed|3)) || !table || table==root
                    || !Mapped(paging,table,4096,true,types)) return false;
                for (uint32_t other=0;other<4;++other)
                    if ((pdpt[other]&address)==table) return false;
            }
        }
        return true;
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
        bool pae, restoreTcgMtrrs;
        // The session every park acknowledgement from this pool is stamped with.
        uint32_t session;
        CpuMemoryTypes memoryTypes;
        Worker* byApic[256];
        Worker* workers[256];
    };
} }
namespace {
    CpuWorkPoolShared* activePool=0;
    // A monotonically increasing session stamp. A park acknowledgement carries the session it belongs to, so a
    // confirmation written by an earlier run can never be read as this one's.
    uint32_t nextSession=1;
    // One real worker step. The AP loop calls this and the host seam calls this, so a test that observes Running is
    // observing the production transition rather than a state someone wrote.
    // One real worker step. The AP loop runs it to completion in a single call; the host seam splits it at the
    // interior boundary so a test can interleave a close or a park. Both paths publish the same states in the same
    // order, and neither can write a terminal state the queue did not produce.
    //
    // Returns true when the step stopped at the boundary and a resume is owed.
    bool WorkerStepBegin(Worker& worker, CpuWorkPoolShared* pool) {
        (void)pool;
        if(!worker.queue.HasPending()){
            SetState(worker,CpuWorkerIdle);
            return false;
        }
        SetState(worker,CpuWorkerBusy);
#if defined(GTOS_CPU_WORK_POOL_TEST) || defined(GTOS_CPU_WORK_POOL_HOST_TEST)
        // Interior boundary: Running has been published and the job has not been touched yet.
        if (__atomic_load_n(&worker.stepGate, __ATOMIC_ACQUIRE)) {
            __atomic_store_n(&worker.stepReached, 1U, __ATOMIC_RELEASE);
            return true;
        }
#endif
        worker.queue.ExecuteOne(Identity());
        return false;
    }
    void WorkerStepFinish(Worker& worker) {
#if defined(GTOS_CPU_WORK_POOL_TEST) || defined(GTOS_CPU_WORK_POOL_HOST_TEST)
        __atomic_store_n(&worker.stepReached, 0U, __ATOMIC_RELEASE);
#endif
        worker.queue.ExecuteOne(Identity());
        if(!worker.queue.HasPending())SetState(worker,CpuWorkerIdle);
    }
    // The whole step, as the AP loop runs it.
    void WorkerStep(Worker& worker, CpuWorkPoolShared* pool) {
        if (WorkerStepBegin(worker, pool)) WorkerStepFinish(worker);
    }
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
CpuWorkTicket::CpuWorkTicket():workerIndex(0xFFFFFFFFU),session(0),queueTag(0),ticket(){}
CpuWorkPool::CpuWorkPool():shared(0),startup(0),error(CpuWorkPoolOk),prepared(false),started(false),closed(false),sessionStamp(0){}
CpuWorkHolder::CpuWorkHolder():pool(0),workerIndex(0xFFFFFFFFU),apicId(0xFFFFFFFFU),ticket(),session(0){}
bool CpuWorkHolder::IsBound()const{
    return pool!=0&&workerIndex!=0xFFFFFFFFU&&apicId!=0xFFFFFFFFU&&session!=0&&ticket.IsValid();
}
bool CpuWorkHolder::BoundTo(uint32_t poolSession,uint32_t index)const{
    return IsBound()&&session==poolSession&&workerIndex==index;
}
bool CpuWorkPool::OnBootstrapProcessor()const{return shared&&(!shared->count||Identity()==shared->bspApicId);}
int CpuWorkPool::FindWorker(uint32_t apicId)const{
    if (!shared) return -1;
    for (uint32_t i=0;i<shared->count;++i)
        if (shared->workers[i]->apicId==apicId) return i;
    return -1;
}
bool CpuWorkPool::Prepare(CpuStartup& boot,memory::PhysicalMemoryManager& frames,bool restoreTcgMtrrs){
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
    shared->restoreTcgMtrrs=restoreTcgMtrrs;
    shared->allocationBase=address;shared->allocationPages=pages;
    shared->session=nextSession++;
    if (candidates) shared->memoryTypes=memoryTypes;
    uint32_t next=((address+sizeof(CpuWorkPoolShared)+63)&~63U);
    for(uint32_t i=0;i<report.detectedProcessors;++i){
        CpuStartupProcessorInfo cpu;if(!boot.GetProcessor(i,cpu)||!cpu.stackPages)continue;
        if(cpu.apicId>=255||cpu.apicId==shared->bspApicId)continue;
        Worker* worker=new((void*)next) Worker;next+=sizeof(Worker);
        worker->apicId=cpu.apicId;worker->stackBase=cpu.stackBase;worker->stackPages=cpu.stackPages;
        // Unique within the pool, so a raw (slot, id) pair is addressable on exactly one worker.
        worker->queueTag=0x51540000U|(shared->session<<8)|(shared->count&0xFFU);
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
    if (!PrepareApplicationProcessorMemory(pool->memoryTypes,&worker->memoryTypeFailure,pool->restoreTcgMtrrs)) {
        Store(worker->failure,CpuWorkerMemoryTypeMismatch); SetState(*worker,CpuWorkerFaulted);
        for (;;) asm volatile("cli; hlt");
    }
    if (pool->pae && !(featureD & (1U<<6))) {
        Store(worker->failure,CpuWorkerPagingMismatch); SetState(*worker,CpuWorkerFaulted);
        for (;;) asm volatile("cli; hlt");
    }
    const uint32_t cr4=(Cr4()&~Cr4Pae)|(pool->pae?Cr4Pae:0U);
    asm volatile("movl %0,%%cr4"::"r"(cr4):"memory");
    asm volatile("movl %0,%%cr3"::"r"(pool->directory):"memory");
    uint32_t control=Cr0()|0x80010000U;asm volatile("movl %0,%%cr0"::"r"(control):"memory");
    Store(worker->observedCr3,Cr3());Store(worker->observedCr0,Cr0());Store(worker->observedCr4,Cr4());
    if(!PagingReady(*worker,pool->directory,pool->pae)){
        Store(worker->failure,CpuWorkerPagingMismatch);SetState(*worker,CpuWorkerFaulted);
        for(;;)asm volatile("cli; hlt");
    }
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
        // Park protocol. The BSP only *requests*; this AP decides when it has actually left the work path. It
        // acknowledges after this point, which is after its last queue access, and then halts with interrupts
        // clear. A BSP that published a state and re-read it would prove nothing; this write is the AP's own.
        if(__atomic_load_n(&worker->parkRequested,__ATOMIC_ACQUIRE)){
            // Nothing may still be owed to the BSP on this worker. A pending or completed-but-uncollected job means
            // the request arrived too early, and it is refused rather than answered.
            const WorkQueueStats stats=worker->queue.GetStats();
            if(stats.submitted<=stats.collected){
                SetState(*worker,CpuWorkerParked);
                __atomic_store_n(&worker->parkAckedSession,pool->session,__ATOMIC_RELEASE);
                __atomic_fetch_add(&worker->parkAckCount,1U,__ATOMIC_RELAXED);
                for(;;)asm volatile("cli; hlt":::"memory");
            }
        }
        if(!worker->queue.HasPending()){
            SetState(*worker,CpuWorkerIdle);
            asm volatile("sti; hlt":::"memory");
            // An idle AP must still see a park request, so it polls briefly with interrupts enabled before
            // halting. A bare sti/hlt would sleep until an unrelated interrupt arrived, which is how a request
            // can be missed for the rest of the run.
            for(uint32_t idlePoll=0;idlePoll<2000;++idlePoll){
                if(__atomic_load_n(&worker->parkRequested,__ATOMIC_ACQUIRE))break;
                asm volatile("pause");
            }
        }else{
            asm volatile("sti":::"memory");
            // The same step the host seam drives: publish Running, execute one job through the production queue.
            WorkerStep(*worker,pool);
        }
    }
}
bool CpuWorkPool::Start(memory::KernelPaging& paging){
    if(!prepared||started||activePool||!OnBootstrapProcessor()||(Flags()&0x200)||(Cr0()&0x80000000U)){error=CpuWorkPoolBadState;return false;}
    memory::PagingStatistics stats=paging.getStatistics();
    const CpuMemoryTypes* types=shared->count?&shared->memoryTypes:0;
    if(!stats.prepared||!stats.directoryAddress){error=CpuWorkPoolBadPaging;return false;}
    uint32_t cr4 = Cr4();
    if ((!stats.pae && (cr4 & Cr4Pae)) || (uint32_t)&kernel_readonly_end <= (uint32_t)&kernel_readonly_start
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
    uint32_t workerDirectory=stats.directoryAddress;
    if (shared->count) {
        uint32_t bootAddress,bootBytes;
        if (!startup->GetSharedMemoryRange(bootAddress,bootBytes)
            || !Mapped(paging,bootAddress,bootBytes,true,types)
            || !MappedTables(paging,stats.directoryAddress,stats.pae,types)
            || !paging.sealForSharedProcessors(&workerDirectory)
            || (workerDirectory!=stats.directoryAddress && !MappedTables(paging,workerDirectory,stats.pae,types))) {
            error=CpuWorkPoolBadPaging; return false;
        }
    }
    // Publish the owner before delivery so an AP that enters can identify itself and route its interrupts.
    // Publication is one-way: it is never undone, because an AP may already be reading the block.
    error=CpuWorkPoolOk;shared->directory=workerDirectory;shared->pae=stats.pae;activePool=shared;
    // Admission opens only after delivery actually succeeded. Setting it first meant a failed delivery still left the
    // pool claiming it had started, which is a state a caller cannot tell from a real one.
    started=startup->StartPrepared(&ApplicationProcessorMain,shared);
    if(!started){
        // The delivery call failed, but it may have entered APs before failing. Their stacks, IDTs, the trampoline and
        // this shared block are all retained: reclaiming storage an AP might still be executing on is exactly the
        // failure the startup protocol warns about.
        error=CpuWorkPoolStartupFailed;
    }
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
    // `started` is what admission keys off, so it must not survive a Start that reported a failure. The owner stays
    // published and every worker record, stack and table stays alive; only the claim to admit new work is withdrawn.
    if(error!=CpuWorkPoolOk)started=false;
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
    ticket.workerIndex=index;ticket.session=shared->session;ticket.queueTag=shared->workers[index]->queueTag;ticket.ticket=local;
    if(!SendWake(shared,apicId)){error=CpuWorkPoolWakeFailed;return CpuWorkAcceptedWakeFailed;}
    return CpuWorkAccepted;
}
CpuWorkCollectStatus CpuWorkPool::Collect(const CpuWorkTicket& ticket,WorkResult& result){
    Guard guard;if(!OnBootstrapProcessor()||!shared||ticket.workerIndex>=shared->count)return CpuWorkTicketInvalid;
    // A ticket from an earlier pool names a session this pool does not own. Without this check the worker index alone
    // would admit it, and a stale ticket could consume a job it was never issued for.
    if(ticket.session!=shared->session)return CpuWorkTicketInvalid;
    // A ticket whose raw slot and id belong to another worker is refused here. Each worker numbers its own queue from
    // one, so two workers legitimately issue identical (slot, id) pairs; the tag is what tells them apart.
    if(ticket.queueTag!=shared->workers[ticket.workerIndex]->queueTag)return CpuWorkTicketInvalid;
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
bool CpuWorkPool::RequestPark(){
    Guard guard;
    uint32_t requestFailures=0;
    if(!OnBootstrapProcessor()){error=CpuWorkPoolBadState;return false;}
    if(!shared||!started){error=CpuWorkPoolBadState;return false;}
    // The request is only a request. Nothing here writes an acknowledgement and nothing here claims the pool is
    // parked; ObservePark is the only function that reports a confirmation, and it reads what the AP wrote.
    for(uint32_t i=0;i<shared->count;++i){
        Worker& worker=*shared->workers[i];
        if(State(worker)==CpuWorkerFaulted)continue;
        __atomic_store_n(&worker.parkRequested,1U,__ATOMIC_RELEASE);
        if(State(worker)!=CpuWorkerParked)SetState(worker,CpuWorkerParkRequested);
        // A halted AP wakes only on an interrupt, so the request must be delivered as one. Without this the
        // request is written and never observed, which is exactly what the guest showed: requested 1, acked 0.
        if(!SendWake(shared,worker.apicId))++requestFailures;
    }
    // A request that could not be delivered to any worker is a failure, not a partial success.
    if(requestFailures){error=CpuWorkPoolWakeFailed;return false;}
    return true;
}
bool CpuWorkPool::ObservePark(uint32_t& requested,uint32_t& acked)const{
    requested=0;acked=0;
    if(!shared)return false;
    const uint32_t session=shared->session;
    for(uint32_t i=0;i<shared->count;++i){
        const Worker& worker=*shared->workers[i];
        if(State(worker)==CpuWorkerFaulted)continue;
        if(__atomic_load_n(&worker.parkRequested,__ATOMIC_ACQUIRE))++requested;
        // Only an acknowledgement stamped with THIS session counts. One left by an earlier pool carries a different
        // stamp and is ignored, which is the whole reason the session is carried.
        if(session!=0&&__atomic_load_n(&worker.parkAckedSession,__ATOMIC_ACQUIRE)==session)++acked;
    }
    return requested>0&&acked==requested;
}
bool CpuWorkPool::ParkWorkers(){
    Guard guard;
    if(!OnBootstrapProcessor()){error=CpuWorkPoolBadState;return false;}
    if(!shared||!started){error=CpuWorkPoolBadState;return false;}
    // Parking an AP that still holds work would abandon that work, so the pool refuses rather than pretending.
    uint32_t queued=0,busy=0,uncollected=0;
    if(!IsDrained(queued,busy,uncollected)){error=CpuWorkPoolBadState;return false;}
    // ParkWorkers now means "confirmed by every worker", not "a state was published". It requests and then waits for
    // each AP to write its own acknowledgement, bounded so a worker that never leaves cannot hang the BSP.
    if(!RequestPark())return false;
    for(uint32_t poll=0;poll<4000000U;++poll){
        uint32_t requested=0,acked=0;
        if(ObservePark(requested,acked))return true;
        asm volatile("pause");
    }
    error=CpuWorkPoolBadState;
    return false;
}
uint32_t CpuWorkPool::Session()const{return shared?shared->session:sessionStamp;}
bool CpuWorkPool::Started()const{return started;}
bool CpuWorkPool::OwnerPublished()const{return activePool!=0&&activePool==shared;}
bool CpuWorkPool::OwnedRange(uint32_t& address,uint32_t& pages)const{
    address=0;pages=0;
    if(!shared)return false;
    address=shared->allocationBase;pages=shared->allocationPages;
    return address!=0&&pages!=0;
}
const CpuWorkQueue* CpuWorkPool::WorkerQueue(uint32_t workerIndex)const{
    if(!shared||workerIndex>=shared->count)return 0;
    return &shared->workers[workerIndex]->queue;
}
CpuWorkPoolReport CpuWorkPool::GetReport()const{
    CpuWorkPoolReport result={};result.error=error;result.schedulerOnlineProcessors=1;if(!shared)return result;
    result.configuredWorkers=shared->count;result.sharedPageDirectory=shared->directory;
    result.session=shared->session;
    for(uint32_t i=0;i<shared->count;++i){const Worker& worker=*shared->workers[i];uint32_t state=State(worker);
        if(Ready(state)&&PagingReady(worker,shared->directory,shared->pae))++result.readyWorkers;
        if (state==CpuWorkerBusy) ++result.busyWorkers;
        if (state==CpuWorkerFaulted) ++result.faultedWorkers;
        result.completedJobs+=worker.queue.GetStats().completed;
        if(__atomic_load_n(&worker.parkRequested,__ATOMIC_ACQUIRE))++result.parkRequestedWorkers;
        if(shared->session&&__atomic_load_n(&worker.parkAckedSession,__ATOMIC_ACQUIRE)==shared->session)
            ++result.parkAckedWorkers;
    }return result;
}
bool CpuWorkPool::GetWorker(uint32_t index,CpuWorkerSnapshot& result)const{
    if (!shared||index>=shared->count) return false;
    const Worker& worker=*shared->workers[index];
    result.apicId=worker.apicId;result.state=(CpuWorkerState)State(worker);
    result.failure=(CpuWorkerFailure)Load(worker.failure);
    result.memoryTypeFailure={};
    if(result.state==CpuWorkerFaulted && result.failure==CpuWorkerMemoryTypeMismatch)
        result.memoryTypeFailure=worker.memoryTypeFailure;
    result.observedCr3=Load(worker.observedCr3);result.observedCr0=Load(worker.observedCr0);result.observedCr4=Load(worker.observedCr4);
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
    case CpuWorkPoolWakeFailed:return "job queued but wake IPI failed";
    case CpuWorkPoolClosed:return "worker pool closed";}return "unknown worker error";}

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
    hostShared->session = nextSession++;
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
        // The same tag Prepare assigns, so a ticket issued on one worker is unaddressable on the other.
        worker->queueTag = 0x51540000U | (hostShared->session << 8) | (hostShared->count & 0xFFU);
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
extern "C" bool gtos_host_worker_enter_busy(uint32_t workerIndex) {
    if (!hostShared || workerIndex >= hostShared->count) return false;
    SetState(*hostShared->workers[workerIndex], CpuWorkerBusy);
    return true;
}
extern "C" bool gtos_host_worker_leave_busy(uint32_t workerIndex) {
    if (!hostShared || workerIndex >= hostShared->count) return false;
    if (State(*hostShared->workers[workerIndex]) != CpuWorkerBusy) return false;
    SetState(*hostShared->workers[workerIndex], CpuWorkerIdle);
    return true;
}
extern "C" bool gtos_host_worker_fault(uint32_t workerIndex) {
    if (!hostShared || workerIndex >= hostShared->count) return false;
    Worker* worker = hostShared->workers[workerIndex];
    Store(worker->failure, CpuWorkerSelfTestMismatch);
    SetState(*worker, CpuWorkerFaulted);
    return true;
}
// The AP's own park sequence, in the AP's own order: leave the work path, then stamp the session, then halt. A test that
// called this without a request would be performing an acknowledgement nobody asked for, so it refuses.
extern "C" bool gtos_host_worker_park(uint32_t workerIndex) {
    if (!hostShared || workerIndex >= hostShared->count) return false;
    Worker* worker = hostShared->workers[workerIndex];
    if (!__atomic_load_n(&worker->parkRequested, __ATOMIC_ACQUIRE)) return false;
    const WorkQueueStats stats = worker->queue.GetStats();
    if (stats.submitted > stats.collected) return false;
    SetState(*worker, CpuWorkerParked);
    __atomic_store_n(&worker->parkAckedSession, hostShared->session, __ATOMIC_RELEASE);
    __atomic_fetch_add(&worker->parkAckCount, 1U, __ATOMIC_RELAXED);
    return true;
}
// Plants an acknowledgement carrying a different session, standing in for a value left by an earlier pool.
extern "C" bool gtos_host_worker_park_stale(uint32_t workerIndex, uint32_t session) {
    if (!hostShared || workerIndex >= hostShared->count) return false;
    __atomic_store_n(&hostShared->workers[workerIndex]->parkAckedSession, session, __ATOMIC_RELEASE);
    return true;
}
extern "C" uint32_t gtos_host_park_requested(uint32_t workerIndex) {
    if (!hostShared || workerIndex >= hostShared->count) return 0xFFFFFFFFU;
    return __atomic_load_n(&hostShared->workers[workerIndex]->parkRequested, __ATOMIC_ACQUIRE);
}
extern "C" uint32_t gtos_host_park_acked_session(uint32_t workerIndex) {
    if (!hostShared || workerIndex >= hostShared->count) return 0xFFFFFFFFU;
    return __atomic_load_n(&hostShared->workers[workerIndex]->parkAckedSession, __ATOMIC_ACQUIRE);
}
// The real step, driven from the test. It calls the same function the AP loop calls, so the Running state a test
// observes is the production transition.
extern "C" bool gtos_host_worker_step(uint32_t workerIndex) {
    if (!hostShared || workerIndex >= hostShared->count) return false;
    return WorkerStepBegin(*hostShared->workers[workerIndex], hostShared);
}
extern "C" bool gtos_host_worker_resume(uint32_t workerIndex) {
    if (!hostShared || workerIndex >= hostShared->count) return false;
    WorkerStepFinish(*hostShared->workers[workerIndex]);
    return true;
}
extern "C" void gtos_host_step_gate(uint32_t armed) {
    if (!hostShared) return;
    for (uint32_t i = 0; i < hostShared->count; ++i)
        __atomic_store_n(&hostShared->workers[i]->stepGate, armed ? 1U : 0U, __ATOMIC_RELEASE);
}
extern "C" uint32_t gtos_host_step_reached() {
    if (!hostShared) return 0;
    uint32_t reached = 0;
    for (uint32_t i = 0; i < hostShared->count; ++i)
        if (__atomic_load_n(&hostShared->workers[i]->stepReached, __ATOMIC_ACQUIRE)) ++reached;
    return reached;
}
// One AP reaching its idle publication. In production the AP writes these itself after its self test; on the host the
// startup boundary calls this for each processor it reports as having entered, so the readiness accounting in Start()
// still runs against real worker records rather than being handed a finished state.
extern "C" bool gtos_host_ap_enter(uint32_t apicId) {
    // A pool built by the real Prepare publishes its owner; an installed one has hostShared. Either way the worker
    // record is the same record the production readiness accounting reads.
    CpuWorkPoolShared* pool = activePool ? activePool : hostShared;
    if (!pool) return false;
    if (apicId >= 256 || !pool->byApic[apicId]) return false;
    Worker* worker = pool->byApic[apicId];
    if (State(*worker) != CpuWorkerPrepared) return false;
    Store(worker->observedCr3, pool->directory);
    Store(worker->observedCr0, 0x80010011U);
    Store(worker->observedCr4, pool->pae ? Cr4Pae : 0);
    SetState(*worker, CpuWorkerIdle);
    return true;
}
extern "C" void gtos_host_reset_pool() {
    // Only the test may reset, and only between independent cases; the production pool never resets.
    hostShared = 0;
    activePool = 0;
}
#endif
