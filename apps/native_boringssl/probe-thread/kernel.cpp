#include "fixture-helpers.h"
#include "record.h"
namespace {
void Finish(bool pass) __attribute__((noreturn));
void Finish(bool pass) {
    printf(pass ? (char*)"NATIVE CRYPTO THREAD PASS\n" : (char*)"NATIVE CRYPTO THREAD FAIL\n");
    asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
    for (;;) asm volatile("cli; hlt");
}
void Need(bool pass,const char* why) {
    if (!pass) { asm volatile("cli" : : : "memory"); printf((char*)"FAILED CRYPTO THREAD "); printf((char*)why); printf((char*)"\n"); Finish(false); }
}
void Value(const char* name,unsigned value) { printf((char*)name); printfHex32(value); }
CryptoThreadRecord Record(NativeRuntime& runtime,unsigned id) {
    CryptoThreadRecord record={}; Need(runtime.ReadMemory(id,CRYPTO_THREAD_RECORD_VA,&record,sizeof(record)),"checked crypto thread record"); return record;
}
NativeStatus Wait(NativeRuntime& runtime,TaskManager& tasks,unsigned id,bool retained) {
    const unsigned begin=tasks.Ticks(); unsigned observed=begin;
    for (;;) {
        NativeStatus status={}; Need(runtime.Status(id,status),"root status"); const auto record=Record(runtime,id);
        if (record.error) {
            Value(" error_line=",record.error); Value(" scenario=",record.scenario); Value(" wave=",record.wave);
            Value(" next=",record.snapshotNext); Value(" serving=",record.snapshotServing); Value(" readers=",record.snapshotReaders);
            for (unsigned i=0;i<3;++i) Value(" worker_stage=",record.worker[i].stage); printf((char*)"\n");
        }
        Need(!record.error,"consumer assertions");
        if (retained ? record.stage==10 : !status.live) return status;
        if (tasks.Ticks()-observed>=256) {
            observed=tasks.Ticks(); Value("CRYPTO THREAD PROGRESS ticks=",observed-begin);
            Value(" scenario=",record.scenario); Value(" wave=",record.wave); Value(" stage=",record.stage);
            for (unsigned i=0;i<3;++i) Value(" worker_stage=",record.worker[i].stage); printf((char*)"\n");
        }
        Need(status.live && tasks.Ticks()-begin<20000,"bounded real crypto thread progress"); WaitTicks(tasks,1);
    }
}
}
asm(".section .text.native_crypto_thread_unused_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"NATIVE CRYPTO THREAD BOOT\n"); GlobalDescriptorTable gdt; TaskManager tasks;
    InterruptsManager interrupts(0x20,&gdt,&tasks); SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;
    Need(frames.initialize(multiboot,magic,(unsigned)&kernel_start,(unsigned)&kernel_end),"physical allocator");
    KernelPaging paging; PagingConfig config={(unsigned)&kernel_start,(unsigned)&kernel_end,
        (unsigned)&kernel_readonly_start,(unsigned)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    NativeRuntime runtime; Need(paging.prepareIdentity(frames,config) && runtime.PrepareStacks(paging,frames)
        && paging.enable() && paging.sealForSharedProcessors(),"guarded kernel mappings");
#if defined(GTOS_CRYPTO_DEFAULT_PROBE)
    const bool threaded=false;
#else
    const bool threaded=true;
#endif
    Need(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2,threaded),"real native crypto contexts");
    const unsigned initial=frames.getStatistics().freeFrames,kernelCr3=paging.getStatistics().directoryAddress;
    const auto* boot=(const MultibootInfo*)multiboot; Need((boot->flags&8) && boot->moduleCount==1,"one admitted crypto thread consumer");
    const auto& module=*((const MultibootModule*)boot->modules); Elf32LoadPlan plan={};
    Need(ValidateElf32((const uint8_t*)module.start,module.end-module.start,plan),"real ELF admission"); unsigned offset=0;
    for (unsigned i=0;i<plan.segmentCount;++i) if (plan.segments[i].virtualAddress==CRYPTO_THREAD_RECORD_VA) offset=plan.segments[i].fileOffset;
    Need(offset && module.end-module.start>=sizeof(CryptoThreadRecord) && offset<=module.end-module.start-sizeof(CryptoThreadRecord)
        && Get32((const uint8_t*)module.start+offset)==1,"bounded crypto thread record");
    Task ring0(&gdt,Ring0Task); Need(tasks.AddTask(&ring0),"ring0 continuation");
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate(); WaitTicks(tasks,8);
    for (unsigned mode=0;mode<(threaded ? 16U : 1U);++mode) {
        CryptoThreadRecord fresh={}; fresh.version=1;
        for (unsigned i=0;i<sizeof(fresh);++i) ((uint8_t*)module.start)[offset+i]=((const uint8_t*)&fresh)[i];
        Put32((uint8_t*)module.start+offset+4,mode);
        const NativeFpStatistics before=runtime.FpStatistics(); const unsigned ringBefore=ring0Progress,bootBefore=tasks.BootTicks(); unsigned id=0;
        Need(runtime.CreateElf((const uint8_t*)module.start,module.end-module.start,id),"fresh native crypto owner group");
        if (mode==13) {
            Wait(runtime,tasks,id,true); Need(runtime.Reap()==0,"live crypto group retained");
            Need(runtime.RequestExit(id,73),"whole crypto TLS group cancel");
        }
        const auto status=Wait(runtime,tasks,id,false); const auto record=Record(runtime,id);
        const unsigned contexts=mode==0 ? (threaded ? 34U : 1U) : mode==9 || mode==10 ? 2U : mode>=13 ? 4U : 1U;
        const unsigned reaps=mode==0 ? 1U : contexts;
        const unsigned exit=mode==0 ? 0U : mode==13 ? 73U : mode==15 ? 0x8000000eU : 0x48000021U;
        Need(!status.live && !status.reaped && status.observedCs==0x23 && status.observedCr3==status.directory
            && status.directory!=kernelCr3 && record.parent==id,"stopped real CPL3 crypto root");
        Value("CRYPTO THREAD STOP mode=",mode); Value(" exit=",status.exitCode); Value(" stage=",record.stage); printf((char*)"\n");
        Need(status.exitCode==exit && record.stage==(mode ? 10U : 20U),"normal/real abort/cancel/OOM/fault outcome");
        if (!mode) {
            Need(record.failedRegistrations==1 && record.failedDestructors==1 && record.successfulRetry==1
                && record.emptyHeap && record.joins==(threaded ? 33U : 0U),"genuine registration OOM ownership/retry and exact joins");
            const auto& root=record.worker[3];
            Need(root.destroyed==6 && root.nullDestroyed==4 && root.reentered==1
                && root.tlsRounds==(threaded ? 136U : 1U),"root snapshot/reentry/new-registration cleanup");
            Need(record.lockScenarios==(threaded ? 2U : 0U) && record.onceEntries==(threaded ? 1U : 0U)
                && record.onceCalls==(threaded ? 4U : 0U),"actual reader/writer/wrap/once scenarios");
            if (threaded) Need(record.wave==CRYPTO_THREAD_WAVES && record.snapshotNext-record.snapshotServing==4
                && !record.snapshotReaders,"all TLS waves and four outstanding writer tickets");
        } else Need(!record.joins && !record.emptyHeap,"no invented abnormal cleanup");
        for (unsigned i=0;i<4;++i) {
            const auto& worker=record.worker[i];
            if (mode) Need(!worker.destroyed && !worker.nullDestroyed && !worker.reentered,"no invented fault/cancel/OOM callbacks");
            if ((!mode && threaded && i<3) || (mode>=13 && i<3) || ((mode==9 || mode==10) && i==0)) {
                NativeStatus stopped={}; Need(worker.id && runtime.Status(worker.id,stopped) && !stopped.live
                    && stopped.directory==status.directory && stopped.reaped==(!mode),"real worker ownership and Join consumption");
                if (!mode) Need(worker.destroyed==6 && worker.nullDestroyed==4 && worker.reentered==1 && worker.tlsRounds==16,"last-wave own TLS callbacks");
                if (mode>=13) Need(worker.stage==3 && worker.tlsRounds==16,"four TLS owners live before abnormal outcome");
                if (mode==15 && i==0) Need(stopped.faultVector==14 && stopped.faultError==6
                    && stopped.faultAddress==0x70000000,"real worker write fault");
            }
        }
        Need(runtime.Reap()==reaps && frames.getStatistics().freeFrames==initial && tasks.TaskCount()==1
            && runtime.Reap()==0,"exact crypto group frame/VM/stack recovery");
        const NativeFpStatistics after=runtime.FpStatistics();
        Need(after.initialized-before.initialized==contexts && after.invalidated-before.invalidated==contexts
            && !after.invariantFailures,"every actual native FP owner recovered");
        WaitTicks(tasks,8); Need(ring0Progress!=ringBefore && tasks.BootTicks()>bootBefore,"kernel continuations");
        Value("CRYPTO THREAD CASE mode=",mode); Value(" checks=",record.checks); Value(" contexts=",contexts);
        Value(" joins=",record.joins); Value(" reaps=",reaps); Value(" free=",frames.getStatistics().freeFrames);
        Value(" baseline=",initial); Value(" fp_init=",after.initialized); Value(" fp_invalid=",after.invalidated);
        Value(" failed_reg=",record.failedRegistrations); Value(" failed_destroy=",record.failedDestructors);
        Value(" retry=",record.successfulRetry); printf((char*)"\n");
    }
    Finish(true);
}
