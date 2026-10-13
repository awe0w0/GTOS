#include "kernel_common.h"
#include "entry-symbols.h"
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"NATIVE FOPEN BOOT\n");ObservedDisk bootDisk;FileStore bootStore;disk=&bootDisk;store=&bootStore;
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;fileFrames=&frames;
    FileNeed(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical allocator");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    FileNeed(paging.prepareIdentity(frames,config)&&disk->Identify()&&disk->SectorCount()==2064,"isolated actual ATA geometry");
    FileNeed(store->Format(disk,8,2048)==0&&store->Mount(disk,8,2048)==0&&store->MakeDirectory("/native")==0,"fresh native file namespace");
    NativeFiles files(*store);NativeRuntime runtime;
    FileNeed(runtime.AttachFiles(files)&&runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors(),"actual FILE endpoint and guarded paging");
    FileNeed(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2)&&runtime.FpEnabled()&&runtime.FpError()==NativeFpOk,"real FP owner");
    kernelDirectory=paging.getStatistics().directoryAddress;const uint32_t initial=frames.getStatistics().freeFrames;
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;FileNeed((boot->flags&8)&&boot->moduleCount==1,"one real FOpen ELF");
    const MultibootModule* modules=(const MultibootModule*)boot->modules;
    Task ring0(&gdt,Ring0Task);FileNeed(tasks.AddTask(&ring0),"ring0 peer");FileTap tap(interrupts,runtime,tasks,syscalls);
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate();WaitTicks(tasks,8);
    for (uint32_t mode=0;mode<4;mode++) {
        const bool retained=mode==1||mode==2;const uint32_t loops=mode==3?4U:1U;
        const uint32_t ringBefore=ring0Progress,bootBefore=tasks.BootTicks();const NativeFpStatistics fpBefore=runtime.FpStatistics();
        const NativeFileStatistics fileBefore=files.Statistics();Elf32LoadPlan fixture={};
        FileNeed(ValidateElf32((const uint8_t*)modules[0].start,modules[0].end-modules[0].start,fixture),"bounded admitted fixture");uint32_t offset=0;
        for (uint32_t i=0;i<fixture.segmentCount;i++) { const Elf32LoadSegment& part=fixture.segments[i];if (part.virtualAddress==FILE_RECORD_VA&&part.fileSize>=sizeof(FileRecord)) offset=part.fileOffset; }
        FileNeed(offset&&offset<=modules[0].end-modules[0].start-sizeof(FileRecord)&&Get32((const uint8_t*)modules[0].start+offset)==1,"mode inside admitted RW record");
        Put32((uint8_t*)modules[0].start+offset+4,mode);Elf32LoadPlan plan={};const uint32_t id=Admit(runtime,modules[0],plan),cost=Cost(plan)+(retained?17U:0U);
        if (mode==1) { Wait(runtime,tasks,id,3,false);FileNeed(runtime.RequestExit(id,73),"cancel running process with active FILE"); }
        const NativeStatus status=Wait(runtime,tasks,id,0,true);const FileRecord record=Record(runtime,id);uint32_t entryStage=0,entryResult=0,word=0;
        FileNeed(runtime.ReadMemory(id,ENTRY_STAGE_VA,&entryStage,4)&&runtime.ReadMemory(id,ENTRY_RESULT_VA,&entryResult,4),"actual native-entry state");
        FileNeed(record.mode==mode&&!record.error&&record.mask==72&&record.directory==loops
            &&record.raw_calls==19*loops+(retained?1U:0U)&&record.checks==72*loops+2+(retained?3U:0U),"exact real FOpen and independent byte/resource checks");
        FileNeed(status.observedCs==0x23&&status.observedCr3==status.directory&&status.directory!=kernelDirectory,"actual CPL3 and private CR3");
        if (!retained) FileNeed(record.stage==2&&entryStage==4&&entryResult==0&&!status.exitCode&&!status.faultVector,"normal return with FILE cleanup");
        else {
            FileNeed(record.stage==3&&entryStage==2&&entryResult==0xffffffff&&record.base==0x80000000&&record.vm_handle&&record.file
                &&record.foreign>=record.base&&record.foreign<=record.base+65536-4096&&runtime.ReadMemory(id,record.foreign,&word,4)&&word==0xf0a01234,"retained actual heap input and FILE");
            if (mode==1) FileNeed(status.exitCode==73&&!status.faultVector&&record.progress,"actual running cancellation");
            else FileNeed(status.exitCode==0x8000000e&&status.faultVector==14&&status.faultError==7&&status.faultAddress==0x100000,"contained actual user fault");
        }
        FileNeed(runtime.ReadMemory(id,0x80000000,&word,4)==retained&&store->OpenCount()==(retained?1U:0U),"released normal or retained stopped owner resources");
        FileNeed(!status.live&&!status.reaped&&initial-frames.getStatistics().freeFrames==cost,"exact stopped process and heap cost");
        printf((char*)"FOPEN CASE");Value(" mode=",mode);Value(" id=",id);Value(" stage=",record.stage);Value(" checks=",record.checks);Value(" unit_checks=",record.mask);
        Value(" loops=",loops);Value(" opens=",record.raw_calls);Value(" calls=",callCount[id]);Value(" progress=",record.progress);Value(" open=",store->OpenCount());
        Value(" entry_stage=",entryStage);Value(" entry_result=",entryResult);Value(" cs=",status.observedCs);Value(" cr3=",status.observedCr3);Value(" kernel=",kernelDirectory);
        Value(" exit=",status.exitCode);Value(" vector=",status.faultVector);Value(" pages=",plan.pageCount);Value(" cost=",cost);Value(" free=",frames.getStatistics().freeFrames);Value(" baseline=",initial);printf((char*)"\n");
        FileNeed(runtime.Reap()==1&&frames.getStatistics().freeFrames==initial&&!store->OpenCount(),"exact FILE heap and frame Reap");
        const NativeFileStatistics fileAfter=files.Statistics();const NativeFpStatistics fpAfter=runtime.FpStatistics();
        FileNeed(fileAfter.released==fileBefore.released+(retained?1U:0U)&&fileAfter.reclaims==fileBefore.reclaims+1
            &&fileAfter.reclaimFailures==fileBefore.reclaimFailures,"actual owner release and error-free Reap");
        FileNeed(fpAfter.initialized==fpBefore.initialized+1&&fpAfter.invalidated==fpBefore.invalidated+1&&!fpAfter.invariantFailures,"FP owner recovered");
        WaitTicks(tasks,8);FileNeed(ring0Progress!=ringBefore&&tasks.BootTicks()>bootBefore,"both continuations alive");
        printf((char*)"FOPEN REAP");Value(" id=",id);Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",initial);Value(" open=",store->OpenCount());
        Value(" released=",fileAfter.released);Value(" reclaims=",fileAfter.reclaims);Value(" failures=",fileAfter.reclaimFailures);
        Value(" fp_init=",fpAfter.initialized);Value(" fp_invalid=",fpAfter.invalidated);printf((char*)"\n");
    }
    FileNeed(!ioErrorCount&&store->Unmount()==0&&disk->reads&&disk->writes&&disk->flushes,"real disk path with clean unmount");
    InterruptGuard guard;printf((char*)"FOPEN FINAL");Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",initial);Value(" reaps=",idCount);
    Value(" open=",store->OpenCount());Value(" trap_stack=",maxTrapStack);Value(" reads=",disk->reads);Value(" writes=",disk->writes);Value(" flushes=",disk->flushes);printf((char*)"\n");FileFinish(true);
}
