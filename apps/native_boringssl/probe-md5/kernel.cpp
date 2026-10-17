#include "fixture-helpers.h"
#include "record.h"
namespace {
void Finish(bool pass) __attribute__((noreturn));
void Finish(bool pass) {
    printf(pass ? (char*)"NATIVE CRYPTO MD5 PASS\n" : (char*)"NATIVE CRYPTO MD5 FAIL\n");
    asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
    for (;;) asm volatile("cli; hlt");
}
void Need(bool pass,const char* why) {
    if (!pass) { asm volatile("cli" : : : "memory"); printf((char*)"FAILED CRYPTO MD5 "); printf((char*)why); printf((char*)"\n"); Finish(false); }
}
void Value(const char* name,unsigned value) { printf((char*)name); printfHex32(value); }
CryptoMd5Record Record(NativeRuntime& runtime,unsigned id) {
    CryptoMd5Record record={}; Need(runtime.ReadMemory(id,CRYPTO_RECORD_VA,&record,sizeof(record)),"checked crypto record"); return record;
}
}
asm(".section .text.native_crypto_unused_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"NATIVE CRYPTO MD5 BOOT\n"); GlobalDescriptorTable gdt; TaskManager tasks;
    InterruptsManager interrupts(0x20,&gdt,&tasks); SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;
    Need(frames.initialize(multiboot,magic,(unsigned)&kernel_start,(unsigned)&kernel_end),"physical allocator");
    KernelPaging paging; PagingConfig config={(unsigned)&kernel_start,(unsigned)&kernel_end,
        (unsigned)&kernel_readonly_start,(unsigned)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    NativeRuntime runtime; Need(paging.prepareIdentity(frames,config) && runtime.PrepareStacks(paging,frames)
        && paging.enable() && paging.sealForSharedProcessors(),"guarded kernel mappings");
    Need(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2,false),"real native SSE2 default context");
    const unsigned initial=frames.getStatistics().freeFrames,kernelCr3=paging.getStatistics().directoryAddress;
    const auto* boot=(const MultibootInfo*)multiboot; Need((boot->flags&8) && boot->moduleCount==1,"one admitted crypto consumer");
    const auto& module=*((const MultibootModule*)boot->modules); Elf32LoadPlan plan={};
    Need(ValidateElf32((const uint8_t*)module.start,module.end-module.start,plan),"real ELF admission"); unsigned offset=0;
    for (unsigned i=0;i<plan.segmentCount;++i) if (plan.segments[i].virtualAddress==CRYPTO_RECORD_VA) offset=plan.segments[i].fileOffset;
    Need(offset && module.end-module.start>=sizeof(CryptoMd5Record) && offset<=module.end-module.start-sizeof(CryptoMd5Record)
        && Get32((const uint8_t*)module.start+offset)==1,"bounded crypto record");
    Task ring0(&gdt,Ring0Task); Need(tasks.AddTask(&ring0),"ring0 continuation");
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate(); WaitTicks(tasks,8);
    for (unsigned mode=0;mode<4;++mode) {
        CryptoMd5Record fresh={}; fresh.version=1;
        for (unsigned i=0;i<sizeof(fresh);++i) ((uint8_t*)module.start)[offset+i]=((const uint8_t*)&fresh)[i];
        Put32((uint8_t*)module.start+offset+4,mode);
        const NativeFpStatistics before=runtime.FpStatistics(); const unsigned ringBefore=ring0Progress,bootBefore=tasks.BootTicks(); unsigned id=0;
        Need(runtime.CreateElf((const uint8_t*)module.start,module.end-module.start,id),"fresh native crypto context");
        const unsigned begin=tasks.Ticks(); unsigned observed=begin; NativeStatus status={}; CryptoMd5Record record={};
        for (;;) {
            Need(runtime.Status(id,status),"root status"); record=Record(runtime,id);
            if (record.error) {
                Value(" error_line=",record.error); Value(" row=",record.row); Value(" stride=",record.stride);
                Value(" alignment=",record.alignment); Value(" actual=",record.actual); Value(" expected=",record.expected); printf((char*)"\n");
            }
            Need(!record.error,"consumer assertions");
            if (!status.live) break;
            if (tasks.Ticks()-observed>=256) {
                observed=tasks.Ticks(); Value("CRYPTO MD5 PROGRESS ticks=",observed-begin);
                Value(" mode=",mode); Value(" row=",record.row); Value(" calls=",record.calls); printf((char*)"\n");
            }
            Need(tasks.Ticks()-begin<20000,"bounded crypto progress"); WaitTicks(tasks,1);
        }
        Need(!status.reaped && status.observedCs==0x23 && status.observedCr3==status.directory
            && status.directory!=kernelCr3 && record.parent==id,"stopped real CPL3 address space");
        Need(status.exitCode==(mode ? 0x8000000eU : 0U) && record.stage==(mode ? 10U : 20U),"digest or expected page fault outcome");
        Need(record.calls==(mode ? 160U : 2800U) && record.oneShot==(mode ? 16U : 280U)
            && record.incremental==(mode ? 144U : 2520U),"all actual MD5 paths counted");
        if (mode) Need(status.faultVector==14 && status.faultAddress==record.expectedFault
            && status.faultError==record.faultError,"exact real read/write/readonly page fault");
        Need(runtime.Reap()==1 && frames.getStatistics().freeFrames==initial && tasks.TaskCount()==1
            && runtime.Reap()==0,"exact frame/VM/stack recovery");
        const NativeFpStatistics after=runtime.FpStatistics();
        Need(after.initialized-before.initialized==1 && after.invalidated-before.invalidated==1
            && !after.invariantFailures,"exact FP owner recovery");
        WaitTicks(tasks,8); Need(ring0Progress!=ringBefore && tasks.BootTicks()>bootBefore,"kernel continuations");
        Value("CRYPTO MD5 CASE mode=",mode); Value(" checks=",record.checks); Value(" calls=",record.calls);
        Value(" oneshot=",record.oneShot); Value(" incremental=",record.incremental);
        Value(" free=",frames.getStatistics().freeFrames); Value(" baseline=",initial);
        Value(" fp_init=",after.initialized); Value(" fp_invalid=",after.invalidated);
        Value(" fault=",status.faultAddress); Value(" fault_error=",status.faultError); printf((char*)"\n");
    }
    Finish(true);
}
