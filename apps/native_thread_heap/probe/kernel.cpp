#include "fixture-helpers.h"
#include "record.h"
#include <memory/criticalsection.h>
#include <process/vm_abi.h>
namespace {
unsigned checks, forcedSwitches, lockWaits, userYields;
PhysicalMemoryManager* allocator;
void Finish(bool pass) __attribute__((noreturn));
void Finish(bool pass) {
    printf(pass ? (char*)"NATIVE THREAD HEAP PASS\n" : (char*)"NATIVE THREAD HEAP FAIL\n");
    asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
    for (;;) asm volatile("cli; hlt");
}
void Need(bool pass, const char* why) {
    ++checks;
    if (!pass) { asm volatile("cli" : : : "memory"); printf((char*)"FAILED THREAD HEAP "); printf((char*)why); printf((char*)"\n"); Finish(false); }
}
void Value(const char* name, unsigned value) { printf((char*)name); printfHex32(value); }
HeapRecord Record(NativeRuntime& runtime, unsigned id) {
    HeapRecord r={}; Need(runtime.ReadMemory(id,HEAP_RECORD_VA,&r,sizeof(r)),"shared checked record"); return r;
}
class HeapTap : public InterruptHandler {
    TaskManager& tasks; SyscallHandler& original;
public:
    HeapTap(InterruptsManager& interrupts,TaskManager& manager,SyscallHandler& handler)
        : InterruptHandler(&interrupts,0x80), tasks(manager), original(handler) {}
    uint32_t HandlerInterrupt(uint32_t esp) {
        CPUState* cpu=(CPUState*)esp; const unsigned operation=cpu->eax;
        if (operation!=GTOS_SYS_VM_RESERVE && operation!=GTOS_SYS_YIELD) return original.HandlerInterrupt(esp);
        unsigned cr3,flags; asm volatile("mov %%cr3,%0":"=r"(cr3)); asm volatile("pushfl; popl %0":"=r"(flags));
        Need(cpu->cs==0x23 && !(flags&0x200) && tasks.CurrentTask() && tasks.CurrentTask()->UserMode(),"actual CPL3 trap");
        const unsigned pde=((const unsigned*)cr3)[HEAP_RECORD_VA>>22]; Need((pde&7)==7 && !(pde&0x80),"private record PDE");
        const unsigned pte=((const unsigned*)(pde&~4095U))[(HEAP_RECORD_VA>>12)&1023];
        Need((pte&7)==7 && allocator->isAllocated(pte&~4095U),"owned RW record PTE");
        const HeapRecord r=*(const HeapRecord*)HEAP_RECORD_VA;
        unsigned index=3;
        for (unsigned i=0;i<3;++i) if (cpu->esp>=r.worker[i].stackBase+4096 && cpu->esp<r.worker[i].stackBase+12288) index=i;
        const bool allocation=r.worker[index].phase==1;
        if (operation==GTOS_SYS_YIELD) { ++userYields; if (allocation) ++lockWaits; }
        const uint32_t returned=original.HandlerInterrupt(esp);
        if (operation==GTOS_SYS_VM_RESERVE && allocation && !forcedSwitches && cpu->eax==0) {
            Need(returned==esp,"successful real reserve result"); ++forcedSwitches;
            // Real syscall has completed, while caller's allocator still holds
            // its lock. Force the same scheduler transition a PIT can perform;
            // no result, memory contents, FP or user frame is synthesized.
            return (uint32_t)tasks.Reschedule(cpu);
        }
        return returned;
    }
};
NativeStatus Wait(NativeRuntime& runtime,TaskManager& tasks,unsigned id,bool retained) {
    const unsigned begin=tasks.Ticks();
    for (;;) {
        NativeStatus s={}; Need(runtime.Status(id,s),"root status"); const HeapRecord r=Record(runtime,id);
        if (r.error) { Value(" error_line=",r.error); printf((char*)"\n"); }
        Need(!r.error,"consumer assertions"); if (retained ? r.stage==10 : !s.live) return s;
        Need(s.live && tasks.Ticks()-begin<20000,"bounded real progress"); WaitTicks(tasks,1);
    }
}
}
asm(".section .text.native_heap_unused_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"NATIVE THREAD HEAP BOOT\n"); GlobalDescriptorTable gdt; TaskManager tasks;
    InterruptsManager interrupts(0x20,&gdt,&tasks); SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames; allocator=&frames;
    Need(frames.initialize(multiboot,magic,(unsigned)&kernel_start,(unsigned)&kernel_end),"physical allocator");
    KernelPaging paging; PagingConfig config={(unsigned)&kernel_start,(unsigned)&kernel_end,
        (unsigned)&kernel_readonly_start,(unsigned)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    NativeRuntime runtime; Need(paging.prepareIdentity(frames,config) && runtime.PrepareStacks(paging,frames)
        && paging.enable() && paging.sealForSharedProcessors(),"guarded kernel mappings");
    Need(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2,true),"qualified shared contexts");
    const unsigned initial=frames.getStatistics().freeFrames, kernelCr3=paging.getStatistics().directoryAddress;
    const MultibootInfo* boot=(const MultibootInfo*)multiboot; Need((boot->flags&8) && boot->moduleCount==1,"one genuine heap consumer");
    const MultibootModule& module=*((const MultibootModule*)boot->modules); Elf32LoadPlan plan={};
    Need(ValidateElf32((const uint8_t*)module.start,module.end-module.start,plan),"real admitted ELF"); unsigned offset=0;
    for (unsigned i=0;i<plan.segmentCount;++i) if (plan.segments[i].virtualAddress==HEAP_RECORD_VA) offset=plan.segments[i].fileOffset;
    Need(offset && offset<=module.end-module.start-sizeof(HeapRecord) && Get32((const uint8_t*)module.start+offset)==1,"bounded heap record");
    Task ring0(&gdt,Ring0Task); Need(tasks.AddTask(&ring0),"ring0 continuation"); HeapTap tap(interrupts,tasks,syscalls);
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate(); WaitTicks(tasks,8);
    for (unsigned mode=0;mode<3;++mode) {
        HeapRecord fresh={}; fresh.version=1;
        for (unsigned i=0;i<sizeof(fresh);++i) ((uint8_t*)module.start)[offset+i]=((const uint8_t*)&fresh)[i];
        Put32((uint8_t*)module.start+offset+4,mode); forcedSwitches=lockWaits=userYields=0;
        const NativeFpStatistics before=runtime.FpStatistics(); const unsigned ringBefore=ring0Progress, bootBefore=tasks.BootTicks(); unsigned id=0;
        Need(runtime.CreateElf((const uint8_t*)module.start,module.end-module.start,id),"fresh sharing root");
        if (mode==1) { Wait(runtime,tasks,id,true); Need(runtime.Reap()==0,"live group not reaped"); Need(runtime.RequestExit(id,73),"whole-group cancel with heap live"); }
        const NativeStatus s=Wait(runtime,tasks,id,false); const HeapRecord r=Record(runtime,id);
        printf((char*)"THREAD HEAP STOP"); Value(" mode=",mode); Value(" exit=",s.exitCode); Value(" stage=",r.stage); printf((char*)"\n");
        Need(!s.live && !s.reaped && s.observedCs==0x23 && s.observedCr3==s.directory && s.directory!=kernelCr3,"real stopped user root");
        Need(forcedSwitches==1 && lockWaits && userYields>lockWaits,"forced held-owner switch and real allocator Yield contention");
        Need(mode==0 ? r.stage==20 && !s.exitCode : r.stage==10 && s.exitCode==(mode==1 ? 73U : 0x8000000eU),"normal/cancel/held-lock fault outcome");
        for (unsigned i=0;i<3;++i) {
            NativeStatus worker={}; Need(runtime.Status(r.worker[i].id,worker) && !worker.live && worker.directory==s.directory,"worker group stopped");
            if (!mode) Need(worker.reaped && r.worker[i].rounds==HEAP_ROUNDS && r.worker[i].moved,"real allocator rounds/join");
            else {
                unsigned byte=0; Need(!worker.reaped && r.worker[i].held && runtime.ReadMemory(id,r.worker[i].held,&byte,1)
                    && (byte&255)==i+0x41,"retained actual allocation bytes");
                if (mode==2 && i==0) Need(worker.faultVector==14 && worker.faultError==6 && worker.faultAddress==0x70000000,"real posix_memalign output fault with lock held");
            }
        }
        Need(runtime.Reap()==(mode ? 4U : 1U) && frames.getStatistics().freeFrames==initial && tasks.TaskCount()==1
            && runtime.Reap()==0,"exact shared stack+heap recovery");
        const NativeFpStatistics after=runtime.FpStatistics(); Need(after.initialized-before.initialized==4
            && after.invalidated-before.invalidated==4 && !after.invariantFailures,"all FP ownership recovered");
        WaitTicks(tasks,8); Need(ring0Progress!=ringBefore && tasks.BootTicks()>bootBefore,"kernel continuations alive");
        printf((char*)"THREAD HEAP CASE"); Value(" mode=",mode); Value(" checks=",r.checks); Value(" waits=",lockWaits); Value(" forced=",forcedSwitches);
        Value(" contexts=",4); Value(" reaps=",mode ? 4 : 1); Value(" free=",frames.getStatistics().freeFrames); Value(" baseline=",initial);
        Value(" fp_init=",after.initialized); Value(" fp_invalid=",after.invalidated); printf((char*)"\n");
    }
    Value("THREAD HEAP KERNEL CHECKS=",checks); printf((char*)"\n"); Finish(true);
}
