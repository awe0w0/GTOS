#include "kernel_common.h"
#include "math-record.h"
namespace {
    void TruncateOpen() {
        const uint32_t owner=0xf0000000;
        int32_t writer=store->Open(owner,"/native/truncate-open.bin",FileStore::Write|FileStore::Create|FileStore::Truncate);
        FileNeed(writer>0&&store->WriteFile(owner,writer,"previous",8)==8&&store->Close(owner,writer)==0,"truncate seed committed");
        int32_t reader=store->Open(owner,"/native/truncate-open.bin",FileStore::Read);uint8_t bytes[8];
        FileNeed(reader>0&&store->ReadFile(owner,reader,bytes,3)==3,"existing reader caches previous version");
        writer=store->Open(owner,"/native/truncate-open.bin",FileStore::Read|FileStore::Write|FileStore::Truncate);
        FileNeed(writer>0&&store->Size(owner,writer)==0&&store->Size(owner,reader)==0,"truncate open refreshes old reader size");
        FileNeed(store->Seek(owner,reader,0,1)==3&&store->ReadFile(owner,reader,bytes,sizeof(bytes))==0,"truncate retains old reader cursor and drops old cache");
        int32_t fresh=store->Open(owner,"/native/truncate-open.bin",FileStore::Read);
        FileNeed(fresh>0&&store->Size(owner,fresh)==0&&store->ReadFile(owner,fresh,bytes,sizeof(bytes))==0,"new reader sees committed empty version");
        FileNeed(store->Close(owner,fresh)==0&&store->Close(owner,reader)==0&&store->Close(owner,writer)==0&&!store->OpenCount(),"truncate closes all readers and writer");
        writer=store->Open(owner,"/native/truncate-open.bin",FileStore::Write);
        FileNeed(writer>0&&store->WriteFile(owner,writer,"previous",8)==8&&store->Close(owner,writer)==0,"truncate fault seed committed");
        const uint32_t flushes=disk->flushes;disk->failFlush=true;
        writer=store->Open(owner,"/native/truncate-open.bin",FileStore::Write|FileStore::Truncate);
        disk->failFlush=false;
        FileNeed(writer==LFS_ERR_IO&&disk->flushes>flushes&&!store->OpenCount(),"truncate open propagates real flush error and unlinks failed handle");
        writer=store->Open(owner,"/native/truncate-open.bin",FileStore::Write|FileStore::Truncate);
        FileNeed(writer>0&&store->Size(owner,writer)==0&&store->Close(owner,writer)==0&&!store->OpenCount(),"truncate recovers after sync failure");
        FileNeed(store->Remove("/native/truncate-open.bin")==0,"truncate fixture removed");
        printf((char*)"TRUNCATE OPEN CHECKS PASS\n");
    }
    void Seed() {
        FileNeed(store->Format(disk,8,2048)==0&&store->Mount(disk,8,2048)==0,"explicit isolated ATA format/mount");
        FileNeed(store->MakeDirectory("/native")==0&&store->MakeDirectory("/fixtures")==0,"real fixture namespace");
        GtosResourceReadRequest request={1,1,0,0,256};ResourceReadPlan plan;
        int32_t h=store->Open(0xf0000000,"/fixtures/image.png",FileStore::Write|FileStore::Create|FileStore::Exclusive);
        FileNeed(h>0,"PNG seed create");
        for (uint32_t offset=0;offset<1108;offset+=256) {
            request.offset=offset;FileNeed(ResourcePlan(request,sizeof(request),plan)>0,"immutable PNG source");
            FileNeed(store->WriteFile(0xf0000000,h,plan.source,plan.bytes)==(int32_t)plan.bytes,"actual disk PNG seed");
        }
        FileNeed(store->Close(0xf0000000,h)==0&&!store->OpenCount(),"seed close");
    }
}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"NATIVE C FILE BOOT\n");
    ObservedDisk bootDisk;FileStore bootStore;disk=&bootDisk;store=&bootStore;
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;fileFrames=&frames;
    FileNeed(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical allocator");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    FileNeed(paging.prepareIdentity(frames,config),"kernel template");
    FileNeed(disk->Identify()&&disk->SectorCount()==2064,"real isolated ATA geometry");Seed();TruncateOpen();
    NativeFiles files(*store);NativeRuntime runtime;
    FileNeed(runtime.AttachFiles(files)&&runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors(),"attach files and guarded paging");
    FileNeed(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2)&&runtime.FpEnabled()&&runtime.FpError()==NativeFpOk,"real FP activation");
    kernelDirectory=paging.getStatistics().directoryAddress;
    const uint32_t initial=frames.getStatistics().freeFrames;
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;FileNeed((boot->flags&8)&&boot->moduleCount==1,"one real FILE ELF with six runtime fixture modes");
    const MultibootModule* modules=(const MultibootModule*)boot->modules;
    Task ring0(&gdt,Ring0Task);FileNeed(tasks.AddTask(&ring0),"ring0 peer");
    FileTap tap(interrupts,runtime,tasks,syscalls);
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate();WaitTicks(tasks,8);
    for (uint32_t mode=0;mode<6;mode++) {
        const uint32_t ringBefore=ring0Progress,bootBefore=tasks.BootTicks();
        const NativeFileStatistics before=files.Statistics();const NativeFpStatistics fpBefore=runtime.FpStatistics();
        Elf32LoadPlan fixture={};FileNeed(ValidateElf32((const uint8_t*)modules[0].start,modules[0].end-modules[0].start,fixture),"bounded fixture input");
        uint32_t recordOffset=0;
        for (uint32_t segment=0;segment<fixture.segmentCount;segment++) {
            const Elf32LoadSegment& part=fixture.segments[segment];
            if (part.virtualAddress==FILE_RECORD_VA&&part.fileSize>=sizeof(FileRecord)) recordOffset=part.fileOffset;
        }
        FileNeed(recordOffset&&recordOffset<=modules[0].end-modules[0].start-sizeof(FileRecord),"fixture mode lies in admitted ELF data");
        FileNeed(Get32((const uint8_t*)modules[0].start+recordOffset)==1,"fixture record version");
        Put32((uint8_t*)modules[0].start+recordOffset+4,mode);
        Elf32LoadPlan plan={};uint32_t id=Admit(runtime,modules[0],plan),cost=Cost(plan)+(mode?17U:0U);
        if (mode==2||mode==3) { Wait(runtime,tasks,id,3,false);FileNeed(runtime.RequestExit(id,73),"external cancel"); }
        NativeStatus status=Wait(runtime,tasks,id,0,true);FileRecord record=Record(runtime,id);
        printf((char*)"C FILE CASE");Value(" mode=",mode);Value(" id=",id);Value(" stage=",record.stage);Value(" error=",record.error);
        Value(" checks=",record.checks);Value(" calls=",callCount[id]);Value(" mask=",callMask[id]);Value(" png=",record.png_bytes);Value(" std_checks=",record.directory);Value(" heap=",record.foreign);
        Value(" format_checks=",record.raw_calls);
        Value(" math_checks=",record.mask);
        Value(" cs=",status.observedCs);Value(" cr3=",status.observedCr3);Value(" kernel=",kernelDirectory);
        Value(" exit=",status.exitCode);Value(" vector=",status.faultVector);Value(" pf=",status.faultError);Value(" address=",status.faultAddress);
        Value(" pages=",plan.pageCount);Value(" cost=",cost);Value(" free=",frames.getStatistics().freeFrames);Value(" baseline=",initial);Value(" open=",store->OpenCount());printf((char*)"\n");
        FileNeed(record.mode==mode&&!record.error&&record.checks>10000&&record.directory==46&&record.raw_calls>3000&&record.png_bytes==1108&&record.base==0x80000000&&record.vm_handle,"actual true formatted FILE suite record");
        FileNeed(record.mask==MATH_CASE_CHECKS,"actual numeric boundary cases and FP context record");
        FileNeed(status.observedCs==0x23&&status.observedCr3==status.directory&&status.directory!=kernelDirectory,"actual CPL3 private process");
        if (mode>=4) FileNeed(record.stage==4&&status.faultVector==6&&status.exitCode==0x80000006,"real mutex misuse rejected by trap");
        else if (mode==1) FileNeed(status.faultVector==14&&status.faultError==7&&status.faultAddress==0x100000,"real contained guard fault");
        else FileNeed(!status.faultVector&&status.exitCode==(mode>1?73U:0U),"normal exit or cancel");
        uint32_t heapWord=0;
        FileNeed(record.foreign==(mode?1U:0U)&&runtime.ReadMemory(id,record.base,&heapWord,sizeof(heapWord))==(mode!=0),
                 "actual released normal heap mapping or retained abnormal heap mapping");
        FileNeed(!status.live&&!status.reaped&&initial-frames.getStatistics().freeFrames==cost,"exact stopped heap and process cost");
        FileNeed(store->OpenCount()==(mode?2U:0U)&&files.Statistics().reclaims==before.reclaims,"deferred open owner retained");
        disk->failFlush=mode==3;FileNeed(runtime.Reap()==1,"one safe Reap");disk->failFlush=false;
        NativeFileStatistics after=files.Statistics();NativeFpStatistics fpAfter=runtime.FpStatistics();
        FileNeed(frames.getStatistics().freeFrames==initial&&!store->OpenCount()&&after.released==before.released+(mode?2U:0U),"exact heap/file/frames reclaimed");
        FileNeed(after.reclaimFailures==before.reclaimFailures+(mode==3?1U:0U),"failed Reap flush reported");
        FileNeed(fpAfter.initialized==fpBefore.initialized+1&&fpAfter.invalidated==fpBefore.invalidated+1&&!fpAfter.invariantFailures,"FP owner reclaimed");
        WaitTicks(tasks,8);FileNeed(ring0Progress!=ringBefore&&tasks.BootTicks()>bootBefore,"both continuations alive");
        printf((char*)"C FILE REAP");Value(" id=",id);Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",initial);
        Value(" open=",store->OpenCount());Value(" released=",after.released);Value(" failures=",after.reclaimFailures);
        Value(" fp_init=",fpAfter.initialized);Value(" fp_invalid=",fpAfter.invalidated);printf((char*)"\n");
    }
    FileNeed(ioErrorCount==42,"all six FILE suites propagate seven actual disk errors including formatter and PrintError flush");
    FileNeed(store->Unmount()==0,"clean actual disk unmount");
    InterruptGuard guard;printf((char*)"C FILE FINAL");Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",initial);
    Value(" open=",store->OpenCount());Value(" calls=",files.Statistics().calls);Value(" io_errors=",ioErrorCount);
    Value(" reads=",disk->reads);Value(" writes=",disk->writes);Value(" flushes=",disk->flushes);Value(" trap_stack=",maxTrapStack);printf((char*)"\n");
    FileFinish(true);
}
