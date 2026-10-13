#include "float_common.h"
#include "entry-symbols.h"
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"NATIVE CXA VIRTUAL BOOT\n");
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;fileFrames=&frames;
    FileNeed(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical allocator");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    FileNeed(paging.prepareIdentity(frames,config),"kernel template");NativeRuntime runtime;
    FileNeed(runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors(),"guarded paging");
    FileNeed(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2)&&runtime.FpEnabled()&&runtime.FpError()==NativeFpOk,"real FP owner");
    kernelDirectory=paging.getStatistics().directoryAddress;const uint32_t initial=frames.getStatistics().freeFrames;
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;FileNeed((boot->flags&8)&&boot->moduleCount==1,"one actual ABI consumer ELF");
    const MultibootModule* modules=(const MultibootModule*)boot->modules;
    Task ring0(&gdt,Ring0Task);FileNeed(tasks.AddTask(&ring0),"ring0 peer");FloatTap tap(interrupts,runtime,tasks,syscalls);
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate();WaitTicks(tasks,8);
    for (uint32_t mode=0;mode<8;mode++) {
        const uint32_t ringBefore=ring0Progress,bootBefore=tasks.BootTicks();const NativeFpStatistics fpBefore=runtime.FpStatistics();
        Elf32LoadPlan fixture={};FileNeed(ValidateElf32((const uint8_t*)modules[0].start,modules[0].end-modules[0].start,fixture),"bounded fixture");
        uint32_t offset=0;
        for (uint32_t i=0;i<fixture.segmentCount;i++) {
            const Elf32LoadSegment& part=fixture.segments[i];if (part.virtualAddress==FILE_RECORD_VA&&part.fileSize>=sizeof(FileRecord)) offset=part.fileOffset;
        }
        FileNeed(offset&&offset<=modules[0].end-modules[0].start-sizeof(FileRecord)&&Get32((const uint8_t*)modules[0].start+offset)==1,"admitted mode field");
        Put32((uint8_t*)modules[0].start+offset+4,mode);
        Elf32LoadPlan plan={};uint32_t id=Admit(runtime,modules[0],plan),cost=Cost(plan)+17;
        const NativeStatus status=Wait(runtime,tasks,id,0,true);const FileRecord record=Record(runtime,id);uint32_t entryStage=0,entryResult=0,actualErrno=0,first=0,last=0;
        FileNeed(runtime.ReadMemory(id,ENTRY_STAGE_VA,&entryStage,4)&&runtime.ReadMemory(id,ENTRY_RESULT_VA,&entryResult,4),"actual entry state");
        FileNeed(record.mode==mode&&record.stage==1&&!record.error&&record.checks==4&&record.command==mode/2&&record.mask==((mode&1)?32U:29U),"real no-return consumer");
        FileNeed(record.foreign>=FILE_RECORD_VA&&record.foreign<=FILE_RECORD_VA+4096-4&&!(record.foreign&3)
            &&runtime.ReadMemory(id,record.foreign,&actualErrno,4)&&actualErrno==(mode<2?1234U:33U),"normal errno or pinned generic formatter EDOM");
        FileNeed(record.base==0x80000000&&record.vm_handle&&record.file>=record.base&&record.file<=record.base+65536-4096
            &&runtime.ReadMemory(id,record.file,&first,4)&&runtime.ReadMemory(id,record.file+4092,&last,4)
            &&first==0xc0a01234&&last==0x51a7b00b,"live heap survives abort until Reap");
        FileNeed(status.observedCs==0x23&&status.observedCr3==status.directory&&status.directory!=kernelDirectory,"actual CPL3 private task");
        FileNeed(status.exitCode==0x48000021&&!status.faultVector&&entryStage==2&&entryResult==0xffffffff,"real abort omits entry normal cleanup");
        FileNeed(diagnosticCalls[id]==1&&panicCalls[id]==1&&injectedResults[id]==(mode>=2?1U:0U),"diagnostic not retried after return fault");
        FileNeed(!status.live&&!status.reaped&&initial-frames.getStatistics().freeFrames==cost,"exact retained heap/process cost");
        printf((char*)"\nCXA VIRTUAL CASE");Value(" mode=",mode);Value(" id=",id);Value(" stage=",record.stage);Value(" error=",record.error);Value(" checks=",record.checks);
        Value(" bytes=",record.mask);Value(" errno=",actualErrno);Value(" writes=",diagnosticCalls[id]+panicCalls[id]);Value(" injected=",injectedResults[id]);
        Value(" entry_stage=",entryStage);Value(" entry_result=",entryResult);Value(" cs=",status.observedCs);Value(" cr3=",status.observedCr3);Value(" kernel=",kernelDirectory);
        Value(" exit=",status.exitCode);Value(" vector=",status.faultVector);Value(" pages=",plan.pageCount);Value(" cost=",cost);Value(" free=",frames.getStatistics().freeFrames);Value(" baseline=",initial);printf((char*)"\n");
        FileNeed(runtime.Reap()==1&&frames.getStatistics().freeFrames==initial,"one exact heap and frame Reap");const NativeFpStatistics fpAfter=runtime.FpStatistics();
        FileNeed(fpAfter.initialized==fpBefore.initialized+1&&fpAfter.invalidated==fpBefore.invalidated+1&&!fpAfter.invariantFailures,"FP owner recovered");
        WaitTicks(tasks,8);FileNeed(ring0Progress!=ringBefore&&tasks.BootTicks()>bootBefore,"both continuations survive");
        printf((char*)"CXA VIRTUAL REAP");Value(" id=",id);Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",initial);
        Value(" fp_init=",fpAfter.initialized);Value(" fp_invalid=",fpAfter.invalidated);printf((char*)"\n");
    }
    InterruptGuard guard;printf((char*)"CXA VIRTUAL FINAL");Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",initial);
    Value(" reaps=",idCount);Value(" trap_stack=",maxTrapStack);printf((char*)"\n");FileFinish(true);
}
