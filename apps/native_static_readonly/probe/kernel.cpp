#include "fixture-helpers.h"
#include "record.h"
namespace {
unsigned checks;
void Finish(bool pass) __attribute__((noreturn));
void Finish(bool pass) {
    printf(pass ? (char*)"NATIVE STATIC READONLY PASS\n" : (char*)"NATIVE STATIC READONLY FAIL\n");
    asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
    for (;;) asm volatile("cli; hlt");
}
void Need(bool pass,const char* why) {
    ++checks;
    if (!pass) { asm volatile("cli" : : : "memory"); printf((char*)"FAILED READONLY "); printf((char*)why); printf((char*)"\n"); Finish(false); }
}
void Value(const char* name,unsigned value) { printf((char*)name); printfHex32(value); }
ReadonlyRecord Record(NativeRuntime& runtime,unsigned id) {
    ReadonlyRecord r={}; Need(runtime.ReadMemory(id,READONLY_RECORD_VA,&r,sizeof(r)),"checked readonly record"); return r;
}
NativeStatus Wait(NativeRuntime& runtime,TaskManager& tasks,unsigned id,bool retained) {
    const unsigned begin=tasks.Ticks();
    for (;;) {
        NativeStatus s={}; Need(runtime.Status(id,s),"root status"); const ReadonlyRecord r=Record(runtime,id);
        if (r.error) { Value(" error_line=",r.error); printf((char*)"\n"); }
        Need(!r.error,"consumer assertions"); if (retained ? r.stage==10 : !s.live) return s;
        Need(s.live && tasks.Ticks()-begin<20000,"bounded real readonly progress"); WaitTicks(tasks,1);
    }
}
}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"NATIVE STATIC READONLY BOOT\n"); GlobalDescriptorTable gdt; TaskManager tasks;
    InterruptsManager interrupts(0x20,&gdt,&tasks); SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;
    Need(frames.initialize(multiboot,magic,(unsigned)&kernel_start,(unsigned)&kernel_end),"physical allocator");
    KernelPaging paging; PagingConfig config={(unsigned)&kernel_start,(unsigned)&kernel_end,
        (unsigned)&kernel_readonly_start,(unsigned)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    NativeRuntime runtime; Need(paging.prepareIdentity(frames,config) && runtime.PrepareStacks(paging,frames)
        && paging.enable() && paging.sealForSharedProcessors(),"guarded kernel mappings");
#if defined(GTOS_READONLY_DEFAULT_PROBE)
    const bool threaded=false;
#else
    const bool threaded=true;
#endif
    Need(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2,threaded),"real native contexts");
    const unsigned initial=frames.getStatistics().freeFrames,kernelCr3=paging.getStatistics().directoryAddress;
    const MultibootInfo* boot=(const MultibootInfo*)multiboot; Need((boot->flags&8) && boot->moduleCount==1,"one real V8 flags consumer");
    const MultibootModule& module=*((const MultibootModule*)boot->modules); Elf32LoadPlan plan={};
    Need(ValidateElf32((const uint8_t*)module.start,module.end-module.start,plan),"real admitted ELF"); unsigned offset=0;
    for (unsigned i=0;i<plan.segmentCount;++i) if (plan.segments[i].virtualAddress==READONLY_RECORD_VA) offset=plan.segments[i].fileOffset;
    Need(offset && module.end-module.start>=sizeof(ReadonlyRecord)
        && offset<=module.end-module.start-sizeof(ReadonlyRecord)
        && Get32((const uint8_t*)module.start+offset)==1,"bounded readonly record");
    Task ring0(&gdt,Ring0Task); Need(tasks.AddTask(&ring0),"ring0 continuation");
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate(); WaitTicks(tasks,8);
    for (unsigned mode=0;mode<=4;++mode) {
        if (!threaded && mode==3) continue;
        ReadonlyRecord fresh={}; fresh.version=1;
        for (unsigned i=0;i<sizeof(fresh);++i) ((uint8_t*)module.start)[offset+i]=((const uint8_t*)&fresh)[i];
        Put32((uint8_t*)module.start+offset+4,mode);
        const NativeFpStatistics before=runtime.FpStatistics();
        const unsigned ringBefore=ring0Progress,bootBefore=tasks.BootTicks(); unsigned id=0;
        Need(runtime.CreateElf((const uint8_t*)module.start,module.end-module.start,id),"fresh readonly owner");
        if (mode==2) { Wait(runtime,tasks,id,true); Need(runtime.Reap()==0,"live sealed owner not reaped"); Need(runtime.RequestExit(id,73),"cancel sealed owner"); }
        const NativeStatus s=Wait(runtime,tasks,id,false); const ReadonlyRecord r=Record(runtime,id);
        Need(!s.live && !s.reaped && s.observedCs==0x23 && s.observedCr3==s.directory && s.directory!=kernelCr3,"real stopped CPL3");
        const bool fault=mode==1 || mode==3;
        const unsigned expected=mode==2 ? 73U : fault ? 0x8000000eU : 0U;
        Need(s.exitCode==expected && r.stage==((mode==0 || mode==4) ? 20U : 10U),"normal/cancel/readonly write fault");
        Need(r.version==1 && r.mode==mode && r.checks>=24 && r.flags_address>=0x40040000U
            && !(r.flags_address&4095) && r.flags_bytes && !(r.flags_bytes&4095)
            && r.before==r.after,"actual V8 flags bytes preserved");
        if (mode==1) Need(s.faultVector==14 && s.faultError==7 && s.faultAddress==r.flags_address,"exact main write protection fault");
        if (mode==3) {
            NativeStatus worker={}; Need(r.worker && r.worker_stage==3 && runtime.Status(r.worker,worker),"real shared readonly worker");
            Need(!worker.live && !worker.reaped && worker.directory==s.directory && worker.exitCode==expected
                && worker.observedCs==0x23 && worker.observedCr3==s.directory
                && worker.faultVector==14 && worker.faultError==7 && worker.faultAddress==r.flags_address,
                "exact shared worker write protection fault");
        } else Need(!r.worker,"no extra context");
        const unsigned contexts=mode==3 ? 2U : 1U;
        Need(runtime.Reap()==contexts && frames.getStatistics().freeFrames==initial
            && tasks.TaskCount()==1 && runtime.Reap()==0,"exact readonly task/frame/stack recovery");
        const NativeFpStatistics after=runtime.FpStatistics();
        Need(after.initialized-before.initialized==contexts && after.invalidated-before.invalidated==contexts
            && !after.invariantFailures,"readonly FP owners recovered");
        WaitTicks(tasks,8); Need(ring0Progress!=ringBefore && tasks.BootTicks()>bootBefore,"kernel continuations alive");
        printf((char*)"READONLY CASE"); Value(" mode=",mode); Value(" checks=",r.checks); Value(" contexts=",contexts);
        Value(" reaps=",contexts); Value(" free=",frames.getStatistics().freeFrames); Value(" baseline=",initial);
        Value(" flags_bytes=",r.flags_bytes); Value(" fp_init=",after.initialized); Value(" fp_invalid=",after.invalidated);
        printf((char*)"\n");
    }
    Value("READONLY KERNEL CHECKS=",checks); printf((char*)"\n"); Finish(true);
}
