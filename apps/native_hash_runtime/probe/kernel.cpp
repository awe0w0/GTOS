#include "hash_common.h"
#include "entry-symbols.h"
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"NATIVE HASH BOOT\n");
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;
    FileNeed(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical allocator");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    FileNeed(paging.prepareIdentity(frames,config),"kernel template");NativeRuntime runtime;
    FileNeed(runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors(),"native guarded stack services");
    FileNeed(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2)&&runtime.FpEnabled()&&runtime.FpError()==NativeFpOk,"real FP activation");
    kernelDirectory=paging.getStatistics().directoryAddress;const uint32_t initial=frames.getStatistics().freeFrames;
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;FileNeed((boot->flags&8)&&boot->moduleCount==1,"one real ELF with eight hash modes");
    const MultibootModule* modules=(const MultibootModule*)boot->modules;
    Task ring0(&gdt,Ring0Task);FileNeed(tasks.AddTask(&ring0),"ring0 peer");HashTap tap(interrupts,syscalls);
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate();WaitTicks(tasks,8);
    for (uint32_t mode=0;mode<8;mode++) {
        const bool normal=mode==0||mode==7,retained=mode>=4&&mode<=6;
        const uint32_t ringBefore=ring0Progress,bootBefore=tasks.BootTicks();const NativeFpStatistics fpBefore=runtime.FpStatistics();
        Elf32LoadPlan fixture={};FileNeed(ValidateElf32((const uint8_t*)modules[0].start,modules[0].end-modules[0].start,fixture),"bounded real fixture");
        uint32_t offset=0;
        for (uint32_t i=0;i<fixture.segmentCount;i++) {
            const Elf32LoadSegment& part=fixture.segments[i];if (part.virtualAddress==FILE_RECORD_VA&&part.fileSize>=sizeof(FileRecord)) offset=part.fileOffset;
        }
        FileNeed(offset&&offset<=modules[0].end-modules[0].start-sizeof(FileRecord)&&Get32((const uint8_t*)modules[0].start+offset)==1,"mode lies in admitted data");
        Put32((uint8_t*)modules[0].start+offset+4,mode);
        Elf32LoadPlan plan={};uint32_t id=Admit(runtime,modules[0],plan),cost=Cost(plan)+(retained?17U:0U);
        if (mode==4) { Wait(runtime,tasks,id,3,false);FileNeed(runtime.RequestExit(id,77),"cancel running hash with retained STL nodes"); }
        NativeStatus status=Wait(runtime,tasks,id,0,true);FileRecord record=Record(runtime,id);uint32_t entryStage=0,entryResult=0;
        FileNeed(runtime.ReadMemory(id,ENTRY_STAGE_VA,&entryStage,4)&&runtime.ReadMemory(id,ENTRY_RESULT_VA,&entryResult,4),"actual native-entry state symbols");
        printf((char*)"HASH CASE");Value(" mode=",mode);Value(" id=",id);Value(" stage=",record.stage);Value(" error=",record.error);Value(" checks=",record.checks);
        Value(" prime=",record.mask);Value(" hash=",record.raw_calls);Value(" sort=",record.directory);Value(" container=",record.bad_calls);
        Value(" entry_stage=",entryStage);Value(" entry_result=",entryResult);Value(" progress=",record.progress);
        Value(" cs=",status.observedCs);Value(" cr3=",status.observedCr3);Value(" kernel=",kernelDirectory);
        Value(" exit=",status.exitCode);Value(" vector=",status.faultVector);Value(" pf=",status.faultError);Value(" address=",status.faultAddress);
        Value(" pages=",plan.pageCount);Value(" cost=",cost);Value(" free=",frames.getStatistics().freeFrames);Value(" baseline=",initial);printf((char*)"\n");
        FileNeed(record.mode==mode&&!record.error&&record.mask==4106&&record.raw_calls==114&&record.directory==4768&&record.bad_calls==1416,"exact independent prime, hash, sort and container checks");
        FileNeed(record.checks==(mode==7?11827U:retained?10411U:10409U),"exact check totals for each mode");
        FileNeed(status.observedCs==0x23&&status.observedCr3==status.directory&&status.directory!=kernelDirectory,"actual CPL3 private address space");
        if (normal) FileNeed(record.stage==2&&!status.faultVector&&status.exitCode==0&&entryStage==4&&entryResult==0,"real main return and complete cleanup");
        else {
            FileNeed(record.stage==3&&entryStage==2&&entryResult==0xffffffff,"abnormal main omits entry cleanup");
            if (mode==1) FileNeed(!status.faultVector&&status.exitCode==0x48000021,"actual no-exceptions prime overflow abort");
            else if (mode==2) FileNeed(status.faultVector==14&&status.faultError==4&&status.faultAddress==0x40041000,"hash read crosses unmapped guard");
            else if (mode==3) FileNeed(status.faultVector==14&&status.faultError==7&&status.faultAddress>=record.progress&&status.faultAddress<record.progress+8,"sort write faults on actual readonly values");
            else if (mode==4) FileNeed(!status.faultVector&&status.exitCode==77&&record.progress,"actual running hash cancellation");
            else if (mode==5) FileNeed(!status.faultVector&&status.exitCode==0x48000010,"actual STL new exhausts fixed heap");
            else FileNeed(status.faultVector==14&&status.faultError==7&&status.faultAddress==0x100000,"contained fault with retained STL allocations");
        }
        uint32_t word=0;FileNeed(runtime.ReadMemory(id,0x80000000,&word,4)==retained,"released or retained actual heap");
        if (retained) FileNeed(record.base==0x80000000&&record.vm_handle&&record.foreign==1,"live heap ownership before Reap");
        FileNeed(!status.live&&!status.reaped&&initial-frames.getStatistics().freeFrames==cost,"exact stopped process cost");
        FileNeed(runtime.Reap()==1,"one exact safe Reap");NativeFpStatistics fpAfter=runtime.FpStatistics();
        FileNeed(frames.getStatistics().freeFrames==initial,"heap and frames recovered");
        FileNeed(fpAfter.initialized==fpBefore.initialized+1&&fpAfter.invalidated==fpBefore.invalidated+1&&!fpAfter.invariantFailures,"FP owner recovered");
        WaitTicks(tasks,8);FileNeed(ring0Progress!=ringBefore&&tasks.BootTicks()>bootBefore,"both continuations alive");
        printf((char*)"HASH REAP");Value(" id=",id);Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",initial);
        Value(" fp_init=",fpAfter.initialized);Value(" fp_invalid=",fpAfter.invalidated);printf((char*)"\n");
    }
    InterruptGuard guard;printf((char*)"HASH FINAL");Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",initial);
    Value(" reaps=",idCount);Value(" trap_stack=",maxTrapStack);printf((char*)"\n");FileFinish(true);
}
