#include "kernel_common.h"
namespace {
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
    FileNeed(disk->Identify()&&disk->SectorCount()==2064,"real isolated ATA geometry");Seed();
    NativeFiles files(*store);NativeRuntime runtime;
    FileNeed(runtime.AttachFiles(files)&&runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors(),"attach files and guarded paging");
    FileNeed(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2)&&runtime.FpEnabled()&&runtime.FpError()==NativeFpOk,"real FP activation");
    kernelDirectory=paging.getStatistics().directoryAddress;
    const uint32_t initial=frames.getStatistics().freeFrames;
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;FileNeed((boot->flags&8)&&boot->moduleCount==6,"six real FILE ELF modes");
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
        Elf32LoadPlan plan={};uint32_t id=Admit(runtime,modules[mode],plan),cost=Cost(plan)+(mode?17U:0U);
        if (mode==2||mode==3) { Wait(runtime,tasks,id,3,false);FileNeed(runtime.RequestExit(id,73),"external cancel"); }
        NativeStatus status=Wait(runtime,tasks,id,0,true);FileRecord record=Record(runtime,id);
        printf((char*)"C FILE CASE");Value(" mode=",mode);Value(" id=",id);Value(" stage=",record.stage);Value(" error=",record.error);
        Value(" checks=",record.checks);Value(" calls=",callCount[id]);Value(" mask=",callMask[id]);Value(" png=",record.png_bytes);Value(" std_checks=",record.directory);Value(" heap=",record.foreign);
        Value(" cs=",status.observedCs);Value(" cr3=",status.observedCr3);Value(" kernel=",kernelDirectory);
        Value(" exit=",status.exitCode);Value(" vector=",status.faultVector);Value(" pf=",status.faultError);Value(" address=",status.faultAddress);
        Value(" pages=",plan.pageCount);Value(" cost=",cost);Value(" free=",frames.getStatistics().freeFrames);Value(" baseline=",initial);Value(" open=",store->OpenCount());printf((char*)"\n");
        FileNeed(record.mode==mode&&!record.error&&record.checks>10000&&record.directory==46&&record.png_bytes==1108&&record.base==0x80000000&&record.vm_handle,"actual true FILE suite record");
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
    FileNeed(ioErrorCount==30,"all six true FILE suites propagate five actual disk errors");
    FileNeed(store->Unmount()==0,"clean actual disk unmount");
    InterruptGuard guard;printf((char*)"C FILE FINAL");Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",initial);
    Value(" open=",store->OpenCount());Value(" calls=",files.Statistics().calls);Value(" io_errors=",ioErrorCount);
    Value(" reads=",disk->reads);Value(" writes=",disk->writes);Value(" flushes=",disk->flushes);Value(" trap_stack=",maxTrapStack);printf((char*)"\n");
    FileFinish(true);
}
