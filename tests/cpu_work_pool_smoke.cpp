#include <hardwarecommunication/cpu_work_pool.h>
#include <gdt.h>
using namespace gtos;
using namespace gtos::hardwarecommunication;
extern "C" uint8_t kernel_start,kernel_end,kernel_readonly_start,kernel_readonly_end;
extern "C" void WorkSmokeTimerEntry(),WorkSmokeUnexpectedEntry();
extern "C" { volatile uint32_t workSmokeTicks=0; }
extern "C" bool GtosWorkInjectNullFault(uint32_t apicId);
extern "C" bool GtosWorkHoldForTest(uint32_t apicId);
extern "C" bool GtosWorkHeldForTest(uint32_t apicId);
extern "C" bool GtosCpuCorruptChecksum(uint32_t apicId);
namespace {
    void Print(const char* text){while(*text){asm volatile("outb %0,$0xE9"::"a"(*text));++text;}}
    void Hex(uint32_t value){for(int shift=28;shift>=0;shift-=4){char c="0123456789ABCDEF"[(value>>shift)&15];asm volatile("outb %0,$0xE9"::"a"(c));}}
    void Value(const char* text,uint32_t value){Print(text);Hex(value);Print("\n");}
    void Finish(bool passed)__attribute__((noreturn));
    void Finish(bool passed){Print(passed?"WORK POOL SMOKE PASS\n":"WORK POOL SMOKE FAIL\n");asm volatile("outl %0,%1"::"a"(passed?0x10U:0x20U),"Nd"((uint16_t)0xF4));for(;;)asm volatile("cli;hlt");}
    void Require(bool passed,const char* text){if(!passed){Print("FAILED ");Print(text);Print("\n");Finish(false);}}
    void Out(uint16_t port,uint8_t value){asm volatile("outb %0,%1"::"a"(value),"Nd"(port));}
    bool Contains(const char* text,const char* word){if(!text)return false;for(;*text;++text){uint32_t i=0;while(word[i]&&text[i]==word[i])++i;if(!word[i])return true;}return false;}
    uint32_t Expected(const char* text){if(!text)return 0;for(;*text;++text)if(text[0]=='n'&&text[1]=='='){uint32_t n=0;text+=2;while(*text>='0'&&*text<='9'){n=n*10+*text-'0';++text;}return n;}return 0;}
    struct Gate{uint16_t low,selector;uint8_t zero,access;uint16_t high;}__attribute__((packed));
    Gate idt[256];
    void InstallIdt(uint16_t selector){for(uint32_t i=0;i<256;++i){uint32_t entry=(uint32_t)(i==0x20?WorkSmokeTimerEntry:WorkSmokeUnexpectedEntry);idt[i].low=entry;idt[i].high=entry>>16;idt[i].selector=selector;idt[i].zero=0;idt[i].access=0x8E;}struct Idtr{uint16_t limit;uint32_t base;}__attribute__((packed));Idtr descriptor={sizeof(idt)-1,(uint32_t)idt};asm volatile("lidt %0"::"m"(descriptor):"memory");}
    void StartTimer(){Out(0x20,0x11);Out(0xA0,0x11);Out(0x21,0x20);Out(0xA1,0x28);Out(0x21,4);Out(0xA1,2);Out(0x21,1);Out(0xA1,1);Out(0x21,0xFE);Out(0xA1,0xFF);Out(0x43,0x36);Out(0x40,11931&255);Out(0x40,11931>>8);asm volatile("sti":::"memory");}
    uint32_t Reference(const WorkRequest& job){uint32_t value=job.seed;for(uint32_t i=0;i<job.iterations;++i){if(job.kind==WorkIntegerHash){value=(value^(i+0x9E3779B9U))*16777619U;value^=value>>13;}else value+=i+1;}return value;}
    void WaitCollect(CpuWorkPool& pool,const CpuWorkTicket& ticket,WorkResult& result){uint32_t before=workSmokeTicks;for(;;){CpuWorkCollectStatus status=pool.Collect(ticket,result);if(status==CpuWorkComplete)return;Require(status==CpuWorkPending,"ticket pending or complete");Require(workSmokeTicks-before<200,"job bounded completion");asm volatile("pause");}}
}
extern "C" void WorkSmokeUnexpected(){Print("BSP UNEXPECTED EXCEPTION\n");Finish(false);}
extern "C" void WorkPoolSmoke(void* multiboot,uint32_t magic){
    Print("WORK POOL SMOKE BOOT\n");GlobalDescriptorTable gdt;InstallIdt(gdt.CodeSegmentSelector());
    memory::PhysicalMemoryManager frames;Require(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical memory");
    const memory::MultibootInfo* mbi=(const memory::MultibootInfo*)multiboot;
    const char* command=(mbi->flags&4)?(const char*)mbi->commandLine:0;
    uint32_t expected=Expected(command);bool checksumTest=Contains(command,"bad-checksum");
    bool memoryRejectTest=Contains(command,"memory-compatibility");
    uint32_t limit=((mbi->memUpper+2047)/1024)*1024*1024;
    CpuManager cpu;cpu.Detect(limit);Value("DETECTED ",cpu.DetectedLogicalProcessors());
    Require(!expected||expected==cpu.DetectedLogicalProcessors(),"expected firmware count");
    CpuStartup startup;CpuWorkPool pool;
    Require(!pool.Prepare(startup,frames),"reject unprepared AP resources");
    Require(startup.Prepare(cpu.GetInfo(),frames,limit),"AP resource preparation");
    Require(pool.Prepare(startup,frames),"worker preparation retries successfully");
    memory::KernelPaging unprepared;
    Require(!pool.Start(unprepared)&&pool.GetReport().error==CpuWorkPoolBadPaging,"reject unprepared page tables");
    memory::KernelPaging paging;memory::PagingDeviceRange devices[]={{0xA0000,0x20000},{0xFEE00000,4096}};
    memory::PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,mbi,devices,2};
    Require(paging.prepareIdentity(frames,config),"prepare shared identity CR3");
    if(checksumTest){CpuWorkerSnapshot first;Require(pool.GetWorker(0,first)&&GtosCpuCorruptChecksum(first.apicId),"inject corrupted AP expectation");}
    bool started=pool.Start(paging);Print(CpuWorkPool::ErrorName(pool.GetReport().error));Print("\n");
    CpuWorkPoolReport report=pool.GetReport();Value("WORKERS READY ",report.readyWorkers);Value("WORKERS CONFIGURED ",report.configuredWorkers);
    if(checksumTest){
        Require(!started&&report.error==CpuWorkPoolStartupFailed&&!report.readyWorkers&&report.faultedWorkers==1,"bad self-test cannot enter worker mode");
        CpuWorkerSnapshot failed;pool.GetWorker(0,failed);Require(failed.failure==CpuWorkerSelfTestMismatch,"self-test rejection reason");
        Print("CORRUPTED SELFTEST HANDOFF REJECTED\n");Finish(true);
    }
    if(memoryRejectTest&&!started){
        Require(!started&&report.error==CpuWorkPoolWorkerSetupFailed&&!report.readyWorkers
            &&report.faultedWorkers==report.configuredWorkers,"mismatched memory types reject worker activation");
        for(uint32_t index=0;index<report.configuredWorkers;++index){CpuWorkerSnapshot failed;pool.GetWorker(index,failed);Require(failed.failure==CpuWorkerMemoryTypeMismatch,"memory-type mismatch diagnosis");}
        Print("MTRR MISMATCH REJECTED SAFELY\n");Finish(true);
    }
    if(!started){for(uint32_t index=0;index<report.configuredWorkers;++index){CpuWorkerSnapshot failed;pool.GetWorker(index,failed);Value("FAILED APIC ",failed.apicId);Value("FAILURE REASON ",failed.failure);}}
    Require(started&&report.readyWorkers+1==cpu.DetectedLogicalProcessors(),"every AP worker ready after paging retry");
    Require(!report.configuredWorkers||(paging.getStatistics().sealedForSharing&&!paging.abandon()),"shared page tables sealed before BSP PG");
    Require(paging.enable(),"BSP shared paging activation");StartTimer();
    uint32_t beginTicks=workSmokeTicks;
    static CpuWorkTicket tickets[256][16];
    static uint32_t identities[256];
    for(uint32_t worker=0;worker<report.readyWorkers;++worker){
        CpuWorkerSnapshot snapshot;Require(pool.GetWorker(worker,snapshot),"worker snapshot");identities[worker]=snapshot.apicId;
        Require(snapshot.observedCr3==paging.getStatistics().directoryAddress&&(snapshot.observedCr0&0x80010001U)==0x80010001U,"AP shared paging and WP");
        for(uint32_t job=0;job<16;++job){WorkRequest request(job&1?WorkModularSum:WorkIntegerHash,65536,worker*100+job);Require(pool.Submit(snapshot.apicId,request,tickets[worker][job])==CpuWorkAccepted,"submit bounded job");}
        CpuWorkTicket full;Require(pool.Submit(snapshot.apicId,WorkRequest(WorkIntegerHash,1,1),full)==CpuWorkFull&&!full.ticket.IsValid(),"completed but uncollected slots retain capacity");
    }
    uint32_t completions=0;
    for(uint32_t worker=0;worker<report.readyWorkers;++worker){
        for(int job=15;job>=0;--job){WorkResult result;WaitCollect(pool,tickets[worker][job],result);WorkRequest request(job&1?WorkModularSum:WorkIntegerHash,65536,worker*100+job);Require(result.executingApicId==identities[worker]&&result.value==Reference(request),"actual AP identity and independent result");Require(pool.Collect(tickets[worker][job],result)==CpuWorkTicketInvalid,"stale ticket rejected");++completions;}
    }
    // Small alternating bursts repeatedly cross the atomic idle-check/STI/HLT
    // boundary, then wait for a PIT tick so APs actually return to idle.
    for(uint32_t round=0;round<64&&report.readyWorkers;++round){
        for(uint32_t worker=0;worker<report.readyWorkers;++worker){WorkRequest request(WorkIntegerHash,1+(round&31),round^worker);Require(pool.Submit(identities[worker],request,tickets[worker][0])==CpuWorkAccepted,"wake race submission");}
        for(uint32_t worker=0;worker<report.readyWorkers;++worker){WorkResult result;WaitCollect(pool,tickets[worker][0],result);Require(result.value==Reference(WorkRequest(WorkIntegerHash,1+(round&31),round^worker))&&result.executingApicId==identities[worker],"wake race result");++completions;}
        if((round&7)==0){uint32_t tick=workSmokeTicks;while(workSmokeTicks==tick)asm volatile("sti;hlt");}
    }
    CpuWorkTicket bad;Require(pool.Submit(254,WorkRequest(WorkIntegerHash,1,0),bad)==CpuWorkOffline,"unconfigured CPU rejected");
    if(report.readyWorkers)Require(pool.Submit(identities[0],WorkRequest(WorkIntegerHash,0,0),bad)==CpuWorkInvalid,"invalid job rejected");
    while(workSmokeTicks==beginTicks)asm volatile("sti;hlt");
    report=pool.GetReport();Require(report.schedulerOnlineProcessors==1&&cpu.OnlineProcessors()==1,"workers are not general scheduler CPUs");
    Require(!report.faultedWorkers&&report.completedJobs==completions,"exact job accounting");
    for(uint32_t worker=0;worker<report.readyWorkers;++worker){CpuWorkerSnapshot snapshot;pool.GetWorker(worker,snapshot);Value("WORKER APIC ",snapshot.apicId);Value("WORKER CR0 ",snapshot.observedCr0);Value("WAKE IRQ COUNT ",snapshot.wakeInterrupts);Value("COMPLETED ",snapshot.queue.completed);Require(snapshot.wakeInterrupts>0,"hardware wake IPI delivered");}
    if (report.readyWorkers) {
        uint32_t before=workSmokeTicks;
        Require(GtosWorkHoldForTest(identities[0]),"hold AP for pending-job fault test");
        while (!GtosWorkHeldForTest(identities[0])) { Require(workSmokeTicks-before<200,"AP hold acknowledged"); asm volatile("pause"); }
        for (uint32_t job=0;job<16;++job)
            Require(pool.Submit(identities[0],WorkRequest(WorkIntegerHash,100,job),tickets[0][job])==CpuWorkAccepted,"queue held jobs");
        Require(GtosWorkInjectNullFault(identities[0]),"inject private AP null guard fault");
        while(pool.GetReport().faultedWorkers==0){Require(workSmokeTicks-before<200,"AP fault reported");asm volatile("pause");}
        CpuWorkerSnapshot failed;pool.GetWorker(0,failed);
        Require(failed.state==CpuWorkerFaulted&&failed.faultVector==14&&failed.faultError==2
            &&failed.faultAddress==0,"AP page fault isolated with CR2/error evidence");
        Require(pool.Submit(identities[0],WorkRequest(WorkIntegerHash,1,0),bad)==CpuWorkOffline,"faulted worker rejects jobs");
        for (uint32_t job=0;job<16;++job) { WorkResult unused; Require(pool.Collect(tickets[0][job],unused)==CpuWorkFailed,"pending jobs fail after worker fault"); }
        while(workSmokeTicks==before)asm volatile("sti;hlt");
        if(report.readyWorkers>1){WorkResult result;Require(pool.Submit(identities[1],WorkRequest(WorkModularSum,10,0),bad)==CpuWorkAccepted,"healthy AP after peer fault");WaitCollect(pool,bad,result);Require(result.value==55&&result.executingApicId==identities[1],"healthy AP still completes work");}
        Print("AP FAULT ISOLATION PASS\n");
    }
    Value("BSP PIT TICKS ",workSmokeTicks);Value("TOTAL JOBS ",completions);Finish(true);
}
