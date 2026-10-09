#include "kernel_common.h"
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    ObservedDisk bootDisk;FileStore bootStore;disk=&bootDisk;store=&bootStore;
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;
    const bool attached=(boot->flags&4)&&Contains((const char*)boot->commandLine,"unmounted");
    printf(attached?(char*)"NATIVE FILE BOOT unmounted\n":(char*)"NATIVE FILE BOOT detached\n");
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;fileFrames=&frames;FileNeed(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical allocator");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    FileNeed(paging.prepareIdentity(frames,config),"kernel template");
    NativeFiles files(*store);NativeRuntime runtime;
    if (attached) FileNeed(runtime.AttachFiles(files),"attach real unmounted service");
    FileNeed(runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors(),"guarded sealed paging");
    FileNeed(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2)&&runtime.FpEnabled()&&runtime.FpError()==NativeFpOk
        &&!runtime.AttachFiles(files),"freeze endpoint after activation");
    kernelDirectory=paging.getStatistics().directoryAddress;const uint32_t initial=frames.getStatistics().freeFrames;
    FileTap tap(interrupts,runtime,tasks,syscalls);FileNeed((boot->flags&8)&&boot->moduleCount==1,"one actual unavailable ELF");
    const MultibootModule* modules=(const MultibootModule*)boot->modules;
    Elf32LoadPlan plan;const uint32_t id=Admit(runtime,modules[0],plan);
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate();const NativeStatus status=Wait(runtime,tasks,id,0,true);const FileRecord record=Record(runtime,id);
    FileNeed(record.version==1&&record.mode==6&&record.stage==2&&!record.error&&record.checks==32&&record.raw_calls==32&&record.bad_calls==32
        &&record.mask==0xffff&&callCount[id]==32&&badCount[id]==32&&!record.invalid_io,"every absent/unmounted call returns ENODEV without I/O");
    FileNeed(status.observedCs==0x23&&status.directory==status.observedCr3&&status.directory!=kernelDirectory
        &&!status.faultVector&&!status.exitCode&&!status.live&&!status.reaped&&initial-frames.getStatistics().freeFrames==Cost(plan),"real stopped private caller cost");
    FileNeed(runtime.Reap()==1&&frames.getStatistics().freeFrames==initial,"unavailable owner exact Reap");
    const NativeFileStatistics f=files.Statistics();const NativeFpStatistics p=runtime.FpStatistics();
    printf((char*)"FILE UNAVAILABLE");Value(" attached=",attached);Value(" calls=",f.calls);Value(" observed_calls=",callCount[id]);
    Value(" bad=",badCount[id]);Value(" mask=",callMask[id]);Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",initial);
    Value(" open=",store->OpenCount());Value(" mounted=",store->Mounted());Value(" reclaims=",f.reclaims);Value(" released=",f.released);
    Value(" failures=",f.reclaimFailures);Value(" last=",f.lastReclaimError);Value(" fp_init=",p.initialized);Value(" fp_invalid=",p.invalidated);
    Value(" fp_fail=",p.invariantFailures);Value(" reads=",disk->reads);Value(" writes=",disk->writes);Value(" flushes=",disk->flushes);printf((char*)"\n");
    FileNeed(!disk->reads&&!disk->writes&&!disk->flushes&&!store->Mounted()&&!store->OpenCount()&&f.calls==(attached?32U:0U)
        &&f.reclaims==(attached?1U:0U)&&!f.released&&!f.reclaimFailures&&!f.lastReclaimError
        &&p.initialized==1&&p.invalidated==1&&!p.invariantFailures,"no spurious unavailable reclaim failure or actual disk access");
    FileFinish(true);
}
