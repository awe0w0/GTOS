#include "fixture-helpers.h"
#include "record.h"
namespace {
unsigned checks;
void Finish(bool pass) __attribute__((noreturn));
void Finish(bool pass) {
    printf(pass ? (char*)"NATIVE THREAD TLS PASS\n" : (char*)"NATIVE THREAD TLS FAIL\n");
    asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
    for (;;) asm volatile("cli; hlt");
}
void Need(bool pass,const char* why) {
    ++checks;
    if (!pass) { asm volatile("cli" : : : "memory"); printf((char*)"FAILED THREAD TLS "); printf((char*)why); printf((char*)"\n"); Finish(false); }
}
void Value(const char* name,unsigned value) { printf((char*)name); printfHex32(value); }
TlsRecord Record(NativeRuntime& runtime,unsigned id) {
    TlsRecord r={}; Need(runtime.ReadMemory(id,TLS_RECORD_VA,&r,sizeof(r)),"shared checked TLS record"); return r;
}
NativeStatus Wait(NativeRuntime& runtime,TaskManager& tasks,unsigned id,bool retained) {
    const unsigned begin=tasks.Ticks();
    for (;;) {
        NativeStatus s={}; Need(runtime.Status(id,s),"root status"); const TlsRecord r=Record(runtime,id);
        if (r.error) { Value(" error_line=",r.error); printf((char*)"\n"); }
        Need(!r.error,"consumer assertions"); if (retained ? r.stage==10 : !s.live) return s;
        Need(s.live && tasks.Ticks()-begin<20000,"bounded real TLS progress"); WaitTicks(tasks,1);
    }
}
}
asm(".section .text.native_tls_unused_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"NATIVE THREAD TLS BOOT\n"); GlobalDescriptorTable gdt; TaskManager tasks;
    InterruptsManager interrupts(0x20,&gdt,&tasks); SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;
    Need(frames.initialize(multiboot,magic,(unsigned)&kernel_start,(unsigned)&kernel_end),"physical allocator");
    KernelPaging paging; PagingConfig config={(unsigned)&kernel_start,(unsigned)&kernel_end,
        (unsigned)&kernel_readonly_start,(unsigned)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    NativeRuntime runtime; Need(paging.prepareIdentity(frames,config) && runtime.PrepareStacks(paging,frames)
        && paging.enable() && paging.sealForSharedProcessors(),"guarded kernel mappings");
    Need(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2,true),"real opt-in shared native contexts");
    const unsigned initial=frames.getStatistics().freeFrames,kernelCr3=paging.getStatistics().directoryAddress;
    const MultibootInfo* boot=(const MultibootInfo*)multiboot; Need((boot->flags&8) && boot->moduleCount==1,"one genuine TLS consumer");
    const MultibootModule& module=*((const MultibootModule*)boot->modules); Elf32LoadPlan plan={};
    Need(ValidateElf32((const uint8_t*)module.start,module.end-module.start,plan),"real admitted ELF"); unsigned offset=0;
    for (unsigned i=0;i<plan.segmentCount;++i) if (plan.segments[i].virtualAddress==TLS_RECORD_VA) offset=plan.segments[i].fileOffset;
    Need(offset && offset<=module.end-module.start-sizeof(TlsRecord) && Get32((const uint8_t*)module.start+offset)==1,"bounded TLS record");
    Task ring0(&gdt,Ring0Task); Need(tasks.AddTask(&ring0),"ring0 continuation");
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate(); WaitTicks(tasks,8);
    for (unsigned mode=0;mode<4;++mode) {
        TlsRecord fresh={}; fresh.version=1; fresh.mode=mode;
        for (unsigned i=0;i<sizeof(fresh);++i) ((uint8_t*)module.start)[offset+i]=((const uint8_t*)&fresh)[i];
        Put32((uint8_t*)module.start+offset+4,mode);
        const NativeFpStatistics before=runtime.FpStatistics(); const unsigned ringBefore=ring0Progress,bootBefore=tasks.BootTicks(); unsigned id=0;
        Need(runtime.CreateElf((const uint8_t*)module.start,module.end-module.start,id),"fresh TLS owner group");
        if (mode==1) { Wait(runtime,tasks,id,true); Need(runtime.Reap()==0,"live TLS group not reaped"); Need(runtime.RequestExit(id,73),"whole-group cancel with TLS live"); }
        const NativeStatus s=Wait(runtime,tasks,id,false); const TlsRecord r=Record(runtime,id);
        printf((char*)"THREAD TLS STOP"); Value(" mode=",mode); Value(" exit=",s.exitCode); Value(" stage=",r.stage); printf((char*)"\n");
        Need(!s.live && !s.reaped && s.observedCs==0x23 && s.observedCr3==s.directory && s.directory!=kernelCr3,"real stopped CPL3 root");
        const unsigned expected=mode==1 ? 73U : mode==2 ? 0x48000021U : mode==3 ? 0x8000000eU : 0U;
        Need(s.exitCode==expected && r.stage==(mode ? 10U : 20U),"normal/cancel/actual TLS OOM/fault outcome");
        Need(mode ? !r.joins && !r.emptyHeap : r.joins==3*TLS_WAVES && r.emptyHeap && r.wave==TLS_WAVES-1,"all waves joined and empty heap observed");
        for (unsigned i=0;i<4;++i) {
            const TlsWorkerRecord& w=r.worker[i];
            Need(w.valueAddress && w.blockAddress && !(w.blockAddress&255),"actual generated TLS template/overalignment");
            for (unsigned j=i+1;j<4;++j) Need(w.valueAddress!=r.worker[j].valueAddress && w.blockAddress!=r.worker[j].blockAddress,"actual owner TLS addresses distinct");
            Need(mode ? w.constructions==2 && !w.destructions && !w.callbacks && !w.order :
                w.constructions==3 && w.destructions==3 && w.callbacks==1 && w.order==2341,"own-thread LIFO re-registration or no invented abnormal callbacks");
            Need(w.rounds==(i==3 && !mode ? 16*TLS_WAVES : 16U),"real cross-thread TLS/errno rounds");
            if (i<3) {
                NativeStatus worker={}; Need(runtime.Status(w.id,worker) && !worker.live && worker.directory==s.directory,"actual same-space worker");
                Need(worker.reaped==!mode,"joined workers consumed exactly once");
                if (mode==3 && i==0) Need(worker.faultVector==14 && worker.faultError==6 && worker.faultAddress==0x70000000,"real worker page fault");
            }
        }
        Need(runtime.Reap()==(mode ? 4U : 1U) && frames.getStatistics().freeFrames==initial && tasks.TaskCount()==1
            && runtime.Reap()==0,"exact TLS group frame/stack/heap recovery");
        const unsigned contexts=mode ? 4U : 1+3*TLS_WAVES;
        const NativeFpStatistics after=runtime.FpStatistics(); Need(after.initialized-before.initialized==contexts
            && after.invalidated-before.invalidated==contexts && !after.invariantFailures,"every reused TLS thread FP owner recovered");
        WaitTicks(tasks,8); Need(ring0Progress!=ringBefore && tasks.BootTicks()>bootBefore,"kernel continuations alive");
        printf((char*)"THREAD TLS CASE"); Value(" mode=",mode); Value(" checks=",r.checks); Value(" contexts=",contexts);
        Value(" joins=",r.joins); Value(" reaps=",mode ? 4 : 1); Value(" free=",frames.getStatistics().freeFrames); Value(" baseline=",initial);
        Value(" fp_init=",after.initialized); Value(" fp_invalid=",after.invalidated); printf((char*)"\n");
    }
    Value("THREAD TLS KERNEL CHECKS=",checks); printf((char*)"\n"); Finish(true);
}
