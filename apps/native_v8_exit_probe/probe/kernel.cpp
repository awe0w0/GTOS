#include "kernel_common.h"
namespace {
    void CheckFiles(uint32_t mode) {
        char buffered[]="/native/buffer0",durable[]="/native/durable0";
        buffered[14]=(char)('0'+mode);durable[15]=(char)('0'+mode);
        FileStore::Info info={};
        FileNeed(store->Stat(buffered,info)==0&&info.size==(mode==0?16U:0U),"normal cleanup flushes; immediate/fault/cancel skips user buffer");
        FileNeed(store->Stat(durable,info)==0&&info.size==9,"already committed file survives every stop");
    }
    void Observe(NativeRuntime& runtime,uint32_t id,uint32_t mode,const NativeStatus& status) {
        const FileRecord record=Record(runtime,id);uint32_t stage=0,result=0;
        FileNeed(runtime.ReadMemory(id,record.command,&stage,4)&&runtime.ReadMemory(id,record.png_bytes,&result,4),"real integration-entry state retained");
        FileNeed(record.mode==mode&&!record.error&&record.stage==3&&record.mask==1&&record.foreign==id&&record.base==0x80000000&&record.vm_handle,"real initialized consumer and ID");
        const uint32_t markers=mode==0?1:0;
        FileNeed(record.raw_calls==markers&&record.bad_calls==markers&&record.ack==markers,"actual automatic/TLS/fini destructors execute only on normal return");
        FileNeed(stage==(mode==0?4U:2U)&&result==(mode==0?17U:0xffffffffU),"immediate/fault bypasses actual native entry cleanup");
        FileNeed(status.observedCs==0x23&&status.observedCr3==status.directory&&status.directory!=kernelDirectory,"actual CPL3/private CR3");
        const uint32_t exit=mode==0?17:mode==4?0x80000006:mode==5?73:1;
        FileNeed(!status.live&&!status.reaped&&status.exitCode==exit&&status.faultVector==(mode==4?6U:0U),"exact actual exit or explicit unsupported-service fault");
        printf((char*)"ABSL EXIT CASE");Value(" mode=",mode);Value(" id=",id);Value(" auto=",record.raw_calls);Value(" tls=",record.bad_calls);Value(" fini=",record.ack);
        Value(" entry=",stage);Value(" result=",result);Value(" exit=",status.exitCode);Value(" vector=",status.faultVector);Value(" cs=",status.observedCs);Value(" cr3=",status.directory);printf((char*)"\n");
    }
}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"ABSL EXIT BOOT\n");
    ObservedDisk bootDisk;FileStore bootStore;disk=&bootDisk;store=&bootStore;
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;fileFrames=&frames;
    FileNeed(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical allocator");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    FileNeed(paging.prepareIdentity(frames,config),"kernel template");
    FileNeed(disk->Identify()&&disk->SectorCount()==2064,"isolated ATA geometry");
    FileNeed(store->Format(disk,8,2048)==0&&store->Mount(disk,8,2048)==0&&store->MakeDirectory("/native")==0,"explicit fresh test volume");
    NativeFiles files(*store);NativeRuntime runtime;
    FileNeed(runtime.AttachFiles(files)&&runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors(),"file attachment and guarded paging");
    FileNeed(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2)&&runtime.FpEnabled()&&runtime.FpError()==NativeFpOk,"FP activation");
    kernelDirectory=paging.getStatistics().directoryAddress;const uint32_t initial=frames.getStatistics().freeFrames;
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;FileNeed((boot->flags&8)&&boot->moduleCount==6,"six actual entry/Abseil consumer ELF modes");
    const MultibootModule* modules=(const MultibootModule*)boot->modules;
    Task ring0(&gdt,Ring0Task);FileNeed(tasks.AddTask(&ring0),"ring0 peer");FileTap tap(interrupts,runtime,tasks,syscalls);
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate();WaitTicks(tasks,8);
    Elf32LoadPlan peerPlan={};const uint32_t peer=Admit(runtime,modules[5],peerPlan),peerCost=Cost(peerPlan)+17;
    Wait(runtime,tasks,peer,3,false);FileNeed(initial-frames.getStatistics().freeFrames==peerCost&&store->OpenCount()==2,"live FILE/VM peer exact baseline");
    const uint32_t baseline=frames.getStatistics().freeFrames;
    for (uint32_t mode=0;mode<5;mode++) {
        const uint32_t ringBefore=ring0Progress,bootBefore=tasks.BootTicks(),peerBefore=Record(runtime,peer).progress;
        const NativeFileStatistics before=files.Statistics();const NativeFpStatistics fpBefore=runtime.FpStatistics();
        Elf32LoadPlan plan={};const uint32_t id=Admit(runtime,modules[mode],plan),cost=Cost(plan)+17;
        const NativeStatus status=Wait(runtime,tasks,id,0,true);Observe(runtime,id,mode,status);
        const uint32_t stopped=frames.getStatistics().freeFrames;
        FileNeed(baseline-stopped==cost&&store->OpenCount()==4&&files.Statistics().released==before.released,"stopped resources retained before Reap");
        FileNeed(runtime.Reap()==1&&frames.getStatistics().freeFrames==baseline&&store->OpenCount()==2,"exact target reclamation preserves live peer");
        const NativeFileStatistics after=files.Statistics();const NativeFpStatistics fpAfter=runtime.FpStatistics();
        FileNeed(after.released==before.released+2&&!after.reclaimFailures&&fpAfter.initialized==fpBefore.initialized+1&&fpAfter.invalidated==fpBefore.invalidated+1&&!fpAfter.invariantFailures,"exact file/FP ownership reclaimed");
        CheckFiles(mode);WaitTicks(tasks,8);NativeStatus peerStatus={};
        FileNeed(runtime.Status(peer,peerStatus)&&peerStatus.live&&Record(runtime,peer).progress!=peerBefore&&ring0Progress!=ringBefore&&tasks.BootTicks()>bootBefore,"user peer, ring0 and boot continue after every stop");
        printf((char*)"ABSL EXIT REAP");Value(" mode=",mode);Value(" cost=",cost);Value(" held=",baseline-stopped);Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",baseline);Value(" open=",store->OpenCount());Value(" released=",after.released-before.released);
        Value(" peer_before=",peerBefore);Value(" peer_after=",Record(runtime,peer).progress);Value(" ring_before=",ringBefore);Value(" ring_after=",ring0Progress);Value(" boot_before=",bootBefore);Value(" boot_after=",tasks.BootTicks());printf((char*)"\n");
    }
    FileNeed(runtime.RequestExit(peer,73),"cancel actual live peer");const NativeStatus peerStatus=Wait(runtime,tasks,peer,0,true);Observe(runtime,peer,5,peerStatus);
    FileNeed(runtime.Reap()==1&&frames.getStatistics().freeFrames==initial&&!store->OpenCount()&&!files.Statistics().reclaimFailures,"all resources returned to exact initial baseline");
    CheckFiles(5);const NativeFpStatistics fp=runtime.FpStatistics();FileNeed(fp.initialized==6&&fp.invalidated==6&&!fp.invariantFailures,"all FP owners exactly reclaimed");
    FileNeed(store->Unmount()==0,"clean isolated test volume unmount");
    InterruptGuard guard;printf((char*)"ABSL EXIT FINAL");Value(" checks=",fileChecks);Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",initial);Value(" open=",store->OpenCount());Value(" released=",files.Statistics().released);Value(" trap_stack=",maxTrapStack);printf((char*)"\n");FileFinish(true);
}
