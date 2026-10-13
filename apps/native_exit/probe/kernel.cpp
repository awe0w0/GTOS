#include "kernel_common.h"
#include "entry-symbols.h"
namespace {
    void Contents(const char* path,const char* expected,bool failed=false,uint32_t kind=0) {
        int32_t file=store->Open(0xf0000000,path,FileStore::Read);FileNeed(file>0,"closed output readable by independent kernel owner");
        uint8_t bytes[16];int32_t count=store->ReadFile(0xf0000000,file,bytes,sizeof(bytes));uint32_t length=0;
        while (expected[length]) length++;
        FileNeed(count>=0&&(uint32_t)count<=length&&store->Size(0xf0000000,file)==count,"bounded readable output length after close");
        if (!failed) FileNeed(count==(int32_t)length,"successful close commits complete output");
        for (uint32_t i=0;i<(uint32_t)count;i++) FileNeed(bytes[i]==(uint8_t)expected[i],"actual callback output prefix");
        if (failed) {
            // A real Flush error does not promise that the new bytes commit.
            printf((char*)"EXIT CLOSE FAILURE");Value(" kind=",kind);Value(" bytes=",(uint32_t)count);Value(" requested=",length);printf((char*)"\n");
        }
        FileNeed(store->Close(0xf0000000,file)==0&&!store->OpenCount(),"independent reader closed");
    }
}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"NATIVE EXIT BOOT\n");ObservedDisk bootDisk;FileStore bootStore;disk=&bootDisk;store=&bootStore;
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;fileFrames=&frames;
    FileNeed(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical allocator");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    FileNeed(paging.prepareIdentity(frames,config),"kernel template");
    FileNeed(disk->Identify()&&disk->SectorCount()==2064,"isolated ATA geometry");
    FileNeed(store->Format(disk,8,2048)==0&&store->Mount(disk,8,2048)==0&&store->MakeDirectory("/native")==0,"isolated filesystem");
    NativeFiles files(*store);NativeRuntime runtime;
    FileNeed(runtime.AttachFiles(files)&&runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors(),"native file and guarded stack services");
    FileNeed(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2)&&runtime.FpEnabled()&&runtime.FpError()==NativeFpOk,"real FP activation");
    kernelDirectory=paging.getStatistics().directoryAddress;const uint32_t initial=frames.getStatistics().freeFrames;
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;FileNeed((boot->flags&8)&&boot->moduleCount==1,"one real ELF with seven exit modes");
    const MultibootModule* modules=(const MultibootModule*)boot->modules;
    Task ring0(&gdt,Ring0Task);FileNeed(tasks.AddTask(&ring0),"ring0 peer");FileTap tap(interrupts,runtime,tasks,syscalls);
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate();WaitTicks(tasks,8);
    for (uint32_t mode=0;mode<7;mode++) {
        const bool normal=mode==0||mode==1||mode==5;
        const uint32_t ringBefore=ring0Progress,bootBefore=tasks.BootTicks();
        const NativeFileStatistics before=files.Statistics();const NativeFpStatistics fpBefore=runtime.FpStatistics();
        Elf32LoadPlan fixture={};FileNeed(ValidateElf32((const uint8_t*)modules[0].start,modules[0].end-modules[0].start,fixture),"bounded real fixture");
        uint32_t offset=0;
        for (uint32_t i=0;i<fixture.segmentCount;i++) {
            const Elf32LoadSegment& part=fixture.segments[i];if (part.virtualAddress==FILE_RECORD_VA&&part.fileSize>=sizeof(FileRecord)) offset=part.fileOffset;
        }
        FileNeed(offset&&offset<=modules[0].end-modules[0].start-sizeof(FileRecord)&&Get32((const uint8_t*)modules[0].start+offset)==1,"mode lies in admitted data");
        Put32((uint8_t*)modules[0].start+offset+4,mode);
        Elf32LoadPlan plan={};uint32_t id=Admit(runtime,modules[0],plan),cost=Cost(plan)+(normal?0U:17U);
        if (mode==3||mode==4) { Wait(runtime,tasks,id,mode==3?6U:7U,false);FileNeed(runtime.RequestExit(id,77),"cancel active callback or registered process"); }
        NativeStatus status=Wait(runtime,tasks,id,0,true);FileRecord record=Record(runtime,id);uint32_t entryStage=0,entryResult=0;
        FileNeed(runtime.ReadMemory(id,ENTRY_STAGE_VA,&entryStage,4)&&runtime.ReadMemory(id,ENTRY_RESULT_VA,&entryResult,4),"actual native-entry state symbols");
        printf((char*)"EXIT CASE");Value(" mode=",mode);Value(" id=",id);Value(" stage=",record.stage);Value(" error=",record.error);Value(" checks=",record.checks);
        Value(" registry=",record.mask);Value(" events=",record.raw_calls);Value(" global=",record.directory);Value(" entry_stage=",entryStage);Value(" entry_result=",entryResult);
        Value(" calls=",callCount[id]);Value(" mask=",callMask[id]);Value(" cs=",status.observedCs);Value(" cr3=",status.observedCr3);Value(" kernel=",kernelDirectory);
        Value(" exit=",status.exitCode);Value(" vector=",status.faultVector);Value(" pf=",status.faultError);Value(" address=",status.faultAddress);
        Value(" pages=",plan.pageCount);Value(" cost=",cost);Value(" free=",frames.getStatistics().freeFrames);Value(" baseline=",initial);Value(" open=",store->OpenCount());printf((char*)"\n");
        FileNeed(record.mode==mode&&!record.error&&record.mask==529&&record.checks>record.mask,"real preinit registry and main checks");
        FileNeed(status.observedCs==0x23&&status.observedCr3==status.directory&&status.directory!=kernelDirectory,"actual CPL3 private address space");
        FileNeed(callCount[id]==(normal?6U:2U)&&callMask[id]==(normal?0x15U:1U),"exact native open/write/close calls and mask");
        FileNeed(record.raw_calls==(normal?63U:mode==2||mode==3?3U:0U)&&record.directory==(normal?2U:1U),"exact TLS/C/global/fini callbacks or abnormal omission");
        if (normal) {
            const uint32_t result=mode==0?17U:mode==1?29U:31U;
            FileNeed(record.stage==2&&!status.faultVector&&status.exitCode==result&&entryStage==4&&entryResult==result,"real main-return or C exit status and full entry cleanup");
        } else if (mode==2) FileNeed(record.stage==6&&status.faultVector==14&&status.faultError==7&&status.faultAddress==0x100000&&entryStage==3&&entryResult==17,"contained callback guard fault");
        else if (mode==3||mode==4) FileNeed(record.stage==(mode==3?6U:7U)&&!status.faultVector&&status.exitCode==77&&entryStage==(mode==3?3U:2U),"actual callback/main cancellation");
        else FileNeed(record.stage==7&&!status.faultVector&&status.exitCode==0x48000021&&entryStage==2,"actual abort omits normal cleanup");
        uint32_t word=0;FileNeed(record.base==0x80000000&&record.vm_handle&&record.foreign==(normal?0U:1U)&&runtime.ReadMemory(id,record.base,&word,4)==!normal,"released normal or retained abnormal heap");
        FileNeed(!status.live&&!status.reaped&&initial-frames.getStatistics().freeFrames==cost&&store->OpenCount()==(normal?0U:2U),"exact stopped process heap and handle ownership");
        FileNeed(files.Statistics().reclaims==before.reclaims,"reclaim deferred until Reap");
        FileNeed(runtime.Reap()==1,"one exact safe Reap");NativeFileStatistics after=files.Statistics();NativeFpStatistics fpAfter=runtime.FpStatistics();
        FileNeed(frames.getStatistics().freeFrames==initial&&!store->OpenCount()&&after.released==before.released+(normal?0U:2U)&&after.reclaimFailures==before.reclaimFailures,"heap, frames and all handles recovered");
        FileNeed(fpAfter.initialized==fpBefore.initialized+1&&fpAfter.invalidated==fpBefore.invalidated+1&&!fpAfter.invariantFailures,"FP owner recovered");
        Contents("/native/lifecycle.txt",normal?"MTCGbaN":"",mode==5,0);Contents("/native/lifecycle-stderr.txt",normal?"S":"",mode==5,1);
        WaitTicks(tasks,8);FileNeed(ring0Progress!=ringBefore&&tasks.BootTicks()>bootBefore,"both continuations alive");
        printf((char*)"EXIT REAP");Value(" id=",id);Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",initial);Value(" open=",store->OpenCount());
        Value(" released=",after.released);Value(" failures=",after.reclaimFailures);Value(" fp_init=",fpAfter.initialized);Value(" fp_invalid=",fpAfter.invalidated);printf((char*)"\n");
    }
    FileNeed(ioErrorCount==2,"two real close Flush failures preserve requested C exit code and release both handles");
    FileNeed(store->Unmount()==0,"clean filesystem unmount");InterruptGuard guard;
    printf((char*)"EXIT FINAL");Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",initial);Value(" open=",store->OpenCount());Value(" calls=",files.Statistics().calls);
    Value(" io_errors=",ioErrorCount);Value(" reads=",disk->reads);Value(" writes=",disk->writes);Value(" flushes=",disk->flushes);Value(" trap_stack=",maxTrapStack);printf((char*)"\n");
    FileFinish(true);
}
