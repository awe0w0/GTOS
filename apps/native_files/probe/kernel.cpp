#include "kernel_common.h"
namespace {
    // Fixture coordination only: the production service never uses physical
    // aliases or caller-supplied owners. IF=0, kernel CR3, exact admitted record.
    void Command(NativeRuntime& runtime,uint32_t id,uint32_t handle,uint32_t command) {
        InterruptGuard guard;NativeStatus status={};FileRecord record=Record(runtime,id);
        FileNeed(runtime.Status(id,status)&&status.live&&Directory()==kernelDirectory,"fixture control safe boot context");
        const uint32_t pde=((const uint32_t*)status.directory)[FILE_RECORD_VA>>22];
        FileNeed((pde&7)==7&&!(pde&0x80),"actual private record PDE");
        const uint32_t pte=((const uint32_t*)(pde&~4095U))[(FILE_RECORD_VA>>12)&1023];
        FileNeed((pte&7)==7,"actual writable record PTE");
        volatile FileRecord* target=(volatile FileRecord*)((pte&~4095U)+(FILE_RECORD_VA&4095));
        FileNeed(target->version==record.version&&target->mode==4,"fixture targets peer record only");
        target->foreign=handle;target->command=command;
    }
    void PeerCommand(NativeRuntime& runtime,TaskManager& tasks,uint32_t peer,uint32_t handle,uint32_t command) {
        Command(runtime,peer,handle,command);const uint32_t begin=tasks.Ticks();
        for (;;) {
            FileRecord p;NativeStatus s;
            { InterruptGuard guard;p=Record(runtime,peer);FileNeed(runtime.Status(peer,s)&&s.live&&!p.error,"peer remains live during foreign/stale test"); }
            if (p.ack==command) return;
            FileNeed(tasks.Ticks()-begin<1000,"peer foreign handle deadline");WaitTicks(tasks,1);
        }
    }
    void LogCase(const FileRecord& x,const NativeStatus& s,const Elf32LoadPlan& plan,uint32_t cost,uint32_t baseline,uint32_t open) {
        printf((char*)"FILE CASE");Value(" mode=",x.mode);Value(" id=",s.id);Value(" stage=",x.stage);Value(" error=",x.error);Value(" checks=",x.checks);
        Value(" mask=",x.mask);Value(" calls=",x.raw_calls);Value(" bad=",x.bad_calls);Value(" png=",x.png_bytes);Value(" base=",x.base);Value(" vm=",x.vm_handle);
        Value(" file=",x.file);Value(" dir=",x.directory);Value(" cs=",s.observedCs);Value(" cr3=",s.observedCr3);Value(" kernel=",kernelDirectory);
        Value(" exit=",s.exitCode);Value(" vector=",s.faultVector);Value(" pf=",s.faultError);Value(" address=",s.faultAddress);
        Value(" pages=",plan.pageCount);Value(" cost=",cost);Value(" baseline=",baseline);Value(" free=",fileFrames->getStatistics().freeFrames);
        Value(" open=",open);printf((char*)"\n");
    }
    void LogReap(uint32_t id,NativeFiles& service,NativeRuntime& runtime,uint32_t baseline) {
        const NativeFileStatistics f=service.Statistics();const NativeFpStatistics p=runtime.FpStatistics();
        printf((char*)"FILE REAP");Value(" id=",id);Value(" free=",fileFrames->getStatistics().freeFrames);Value(" expected=",baseline);
        Value(" open=",store->OpenCount());Value(" reclaims=",f.reclaims);Value(" released=",f.released);Value(" failures=",f.reclaimFailures);Value(" last=",f.lastReclaimError);
        Value(" fp_init=",p.initialized);Value(" fp_invalid=",p.invalidated);Value(" fp_fail=",p.invariantFailures);printf((char*)"\n");
    }
    void Seed() {
        FileNeed(store->Format(disk,8,2048)==0&&store->Mount(disk,8,2048)==0,"explicit format only dedicated scratch ATA volume");
        FileNeed(store->MakeDirectory("/native")==0&&store->MakeDirectory("/fixtures")==0,"real fixture namespace");
        GtosResourceReadRequest request={1,1,0,0,256};ResourceReadPlan plan;
        const int32_t h=store->Open(0xf0000000,"/fixtures/image.png",FileStore::Write|FileStore::Create|FileStore::Exclusive);FileNeed(h>0,"fixture PNG create");
        for (uint32_t offset=0;offset<1108;offset+=256) {
            request.offset=offset;FileNeed(ResourcePlan(request,sizeof(request),plan)>0,"immutable PNG chunk");
            FileNeed(store->WriteFile(0xf0000000,h,plan.source,plan.bytes)==(int32_t)plan.bytes,"actual ATA fixture PNG write");
        }
        FileNeed(store->Close(0xf0000000,h)==0&&!store->OpenCount(),"fixture close");
    }
}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    ObservedDisk bootDisk;FileStore bootStore;disk=&bootDisk;store=&bootStore;
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;
    const bool reader=(boot->flags&4)&&Contains((const char*)boot->commandLine,"reader");
    printf(reader?(char*)"NATIVE FILE BOOT reader\n":(char*)"NATIVE FILE BOOT writer\n");
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;fileFrames=&frames;FileNeed(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical allocator");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    FileNeed(paging.prepareIdentity(frames,config),"kernel template");
    FileNeed(disk->Identify()&&disk->SectorCount()==2064,"explicitly identify actual isolated ATA geometry");
    if (reader) FileNeed(store->Mount(disk,8,2048)==0,"cold ATA mount");
    else Seed();
    NativeFiles files(*store);NativeRuntime runtime;FileNeed(runtime.AttachFiles(files)&&!runtime.AttachFiles(files),"attach endpoint once before activation");
    FileNeed(runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors(),"guarded sealed paging");
    FileNeed(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2)&&runtime.FpEnabled()&&runtime.FpError()==NativeFpOk
        &&!runtime.AttachFiles(files),"real FP/core activation freezes endpoint");
    kernelDirectory=paging.getStatistics().directoryAddress;const uint32_t initial=frames.getStatistics().freeFrames;
    FileTap tap(interrupts,runtime,tasks,syscalls);FileNeed((boot->flags&8)&&boot->moduleCount==6,"six actual native ELF modes");
    const MultibootModule* modules=(const MultibootModule*)boot->modules;
    Task ring0(&gdt,Ring0Task);FileNeed(tasks.AddTask(&ring0),"ring0 continuation");
    Elf32LoadPlan peerPlan={};uint32_t peer=0;
    if (!reader) peer=Admit(runtime,modules[4],peerPlan);
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate();
    if (!reader) Wait(runtime,tasks,peer,1,false);
    WaitTicks(tasks,8);
    const uint32_t baseline=frames.getStatistics().freeFrames;
    FileNeed(initial-baseline==(reader?0:Cost(peerPlan))&&store->OpenCount()==(reader?0U:2U),"exact peer VM/file cost");
    for (uint32_t iteration=0;iteration<(reader?1U:5U);iteration++) {
        const uint32_t mode=reader?5U:(iteration==4?0U:iteration);
        const uint32_t progress=reader?0:Record(runtime,peer).progress,ringBefore=ring0Progress,bootBefore=tasks.BootTicks();
        const NativeFileStatistics before=files.Statistics();const NativeFpStatistics fpBefore=runtime.FpStatistics();
        Elf32LoadPlan plan;const uint32_t id=Admit(runtime,modules[mode],plan),cost=Cost(plan)+3;
        if (mode==2||mode==3) { Wait(runtime,tasks,id,3,false);FileNeed(runtime.RequestExit(id,73),"external cancel"); }
        const NativeStatus status=Wait(runtime,tasks,id,0,true);const FileRecord record=Record(runtime,id);
        { InterruptGuard guard;LogCase(record,status,plan,cost,baseline,store->OpenCount()); }
        FileNeed(record.version==1&&record.mode==mode&&!record.error&&record.checks>9000&&record.png_bytes==1108
            &&record.base==0x80000000&&record.vm_handle&&record.raw_calls==callCount[id]&&record.mask==callMask[id]
            &&record.bad_calls==badCount[id]&&!record.invalid_io,"actual retained suite record equals independent trap counts");
        FileNeed(record.stage==(mode==1||mode==2||mode==3?3U:2U)
            &&(reader||(record.mask==0xffff&&record.bad_calls>=100&&record.file&&record.directory)),"all operations and validation families exercised");
        FileNeed(status.observedCs==0x23&&status.observedCr3==status.directory&&status.directory!=kernelDirectory,"CPL3 isolated directory");
        if (mode==1) FileNeed(status.faultVector==14&&status.faultError==4&&status.faultAddress==0x80003000&&status.exitCode==0x8000000e,"real user guard fault contained");
        else FileNeed(!status.faultVector&&status.exitCode==(mode==2||mode==3?73U:0U),"normal exit or external cancellation");
        FileNeed(!status.live&&!status.reaped&&baseline-frames.getStatistics().freeFrames==cost,"exact retained stopped VM cost");
        FileNeed(files.Statistics().reclaims==before.reclaims&&store->OpenCount()==(reader?0U:4U),"all stops defer close until safe Reap");
        if (!reader) {
            FileStore::Info information={};FileNeed(store->HandleInfo(id,record.file,information)==0&&information.size==6
                &&store->HandleInfo(id,record.directory,information)==0&&information.type==LFS_TYPE_DIR,"stopped owner still owns both handles");
            PeerCommand(runtime,tasks,peer,record.file,iteration*2+1);
        }
        disk->failFlush=mode==3;FileNeed(runtime.Reap()==1,"one real deferred Reap");disk->failFlush=false;
        const NativeFileStatistics after=files.Statistics();const NativeFpStatistics fpAfter=runtime.FpStatistics();
        FileNeed(frames.getStatistics().freeFrames==baseline&&store->OpenCount()==(reader?0U:2U)
            &&after.reclaims==before.reclaims+1&&after.released==before.released+(reader?0U:2U),"exact file and VM reclamation with peer preserved");
        FileNeed(after.reclaimFailures==before.reclaimFailures+(mode==3?1U:0U)&&after.lastReclaimError==(mode==3?LFS_ERR_IO:0),"failed flush reported without leaked handle");
        FileNeed(fpAfter.initialized==fpBefore.initialized+1&&fpAfter.invalidated==fpBefore.invalidated+1&&!fpAfter.invariantFailures
            &&fpAfter.saves>fpBefore.saves&&fpAfter.restores>fpBefore.restores,"real FP owner lifecycle");
        if (!reader) {
            FileStore::Info information={};FileNeed(store->HandleInfo(id,record.file,information)==LFS_ERR_BADF
                &&store->HandleInfo(id,record.directory,information)==LFS_ERR_BADF,"reaped descriptors invalid");
            PeerCommand(runtime,tasks,peer,record.file,iteration*2+2);
        }
        WaitTicks(tasks,8);
        FileNeed(ring0Progress!=ringBefore&&tasks.BootTicks()>bootBefore
            &&(reader||(Record(runtime,peer).progress!=progress&&!Record(runtime,peer).error)),"both peers continue after stops and Reap");
        { InterruptGuard guard;LogReap(id,files,runtime,baseline); }
    }
    if (!reader) {
        const FileRecord p=Record(runtime,peer);printf((char*)"FILE PEER");Value(" id=",peer);Value(" error=",p.error);Value(" checks=",p.checks);
        Value(" commands=",p.ack);Value(" bad=",p.bad_calls);Value(" observed_bad=",badCount[peer]);Value(" calls=",p.raw_calls);Value(" observed_calls=",callCount[peer]);printf((char*)"\n");
        FileNeed(!p.error&&p.ack==10&&p.bad_calls==100&&badCount[peer]==100&&p.raw_calls==callCount[peer],"peer rejects every live/stale foreign handle");
        FileNeed(runtime.RequestExit(peer,0)&&runtime.Reap()==1,"peer final safe Reap");
    }
    const NativeFileStatistics f=files.Statistics();const NativeFpStatistics p=runtime.FpStatistics();
    FileNeed(frames.getStatistics().freeFrames==initial&&!store->OpenCount()&&f.reclaims==(reader?1U:6U)&&f.released==(reader?0U:12U)
        &&f.reclaimFailures==(reader?0U:1U)&&p.initialized==(reader?1U:6U)&&p.invalidated==p.initialized&&!p.invariantFailures,"all exact baselines restored");
    FileNeed(ioErrorCount==(reader?0U:15U),"all actual read/write/close error paths");
    FileNeed(store->Unmount()==0,"clean unmount after all native owners reclaimed");
    InterruptGuard guard;printf((char*)"FILE FINAL");Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",initial);Value(" checks=",fileChecks);
    Value(" calls=",f.calls);Value(" open=",store->OpenCount());Value(" reclaims=",f.reclaims);Value(" released=",f.released);Value(" failures=",f.reclaimFailures);
    Value(" fp_init=",p.initialized);Value(" fp_invalid=",p.invalidated);Value(" fp_fail=",p.invariantFailures);Value(" reads=",disk->reads);Value(" writes=",disk->writes);
    Value(" flushes=",disk->flushes);Value(" io_errors=",ioErrorCount);Value(" trap_stack_observed=",maxTrapStack);printf((char*)"\n");FileFinish(true);
}
