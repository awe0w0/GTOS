#include "kernel_common.h"
#include "entry-symbols.h"
namespace {
    void Contents(const char* path,const char* expected,uint32_t length=0) {
        int32_t file=store->Open(0xf0000000,path,FileStore::Read);
        FileNeed(file>0,"closed stream readable by independent owner");
        uint8_t bytes[64];int32_t count=store->ReadFile(0xf0000000,file,bytes,sizeof(bytes));
        if (!length) while (expected[length]) length++;
        if (count!=(int32_t)length) {
            printf((char*)"CONTENT LENGTH ");printf((char*)path);Value(" expected=",length);Value(" actual=",(uint32_t)count);printf((char*)" bytes=");
            for (uint32_t i=0;i<(uint32_t)(count>0?count:0)&&i<sizeof(bytes);i++) printfHex32(bytes[i]);
            printf((char*)"\n");
        }
        FileNeed(length<sizeof(bytes)&&count==(int32_t)length&&store->Size(0xf0000000,file)==count,"exact committed stream length");
        for (uint32_t i=0;i<length;i++) FileNeed(bytes[i]==(uint8_t)expected[i],"exact real narrow and wide output");
        FileNeed(store->Close(0xf0000000,file)==0&&!store->OpenCount(),"independent reader released");
    }
}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"NATIVE IOSTREAM BOOT\n");ObservedDisk bootDisk;FileStore bootStore;disk=&bootDisk;store=&bootStore;
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;fileFrames=&frames;
    FileNeed(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical allocator");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    FileNeed(paging.prepareIdentity(frames,config),"kernel template");
    FileNeed(disk->Identify()&&disk->SectorCount()==2064,"isolated private ATA geometry");
    FileNeed(store->Format(disk,8,2048)==0&&store->Mount(disk,8,2048)==0&&store->MakeDirectory("/native")==0,"isolated filesystem");
    NativeFiles files(*store);NativeRuntime runtime;
    FileNeed(runtime.AttachFiles(files)&&runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors(),"real file and stack services");
    FileNeed(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2)&&runtime.FpEnabled()&&runtime.FpError()==NativeFpOk,"real SSE2 activation");
    kernelDirectory=paging.getStatistics().directoryAddress;const uint32_t initial=frames.getStatistics().freeFrames;
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;FileNeed((boot->flags&8)&&boot->moduleCount==1,"one real entry and standard-stream ELF");
    const MultibootModule* modules=(const MultibootModule*)boot->modules;
    Task ring0(&gdt,Ring0Task);FileNeed(tasks.AddTask(&ring0),"ring0 peer");FileTap tap(interrupts,runtime,tasks,syscalls);
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate();WaitTicks(tasks,8);
    for (uint32_t mode=0;mode<11;mode++) {
        const bool normal=mode<2;
        const uint32_t ringBefore=ring0Progress,bootBefore=tasks.BootTicks();
        const NativeFileStatistics before=files.Statistics();const NativeFpStatistics fpBefore=runtime.FpStatistics();
        Elf32LoadPlan fixture={};FileNeed(ValidateElf32((const uint8_t*)modules[0].start,modules[0].end-modules[0].start,fixture),"bounded actual ELF");
        uint32_t offset=0;
        for (uint32_t i=0;i<fixture.segmentCount;i++) {
            const Elf32LoadSegment& part=fixture.segments[i];
            if (part.virtualAddress==FILE_RECORD_VA&&part.fileSize>=sizeof(FileRecord)) offset=part.fileOffset;
        }
        FileNeed(offset&&offset<=modules[0].end-modules[0].start-sizeof(FileRecord)&&Get32((const uint8_t*)modules[0].start+offset)==1,"mode in admitted data");
        Put32((uint8_t*)modules[0].start+offset+4,mode);
        Elf32LoadPlan plan={};uint32_t id=Admit(runtime,modules[0],plan);
        if (mode>=4) { TemporaryStoppedCase(runtime,tasks,files,id,mode,initial,before,fpBefore);continue; }
        NativeStatus status=Wait(runtime,tasks,id,0,true);FileRecord record=Record(runtime,id);uint32_t entryStage=0,entryResult=0;
        FileNeed(runtime.ReadMemory(id,ENTRY_STAGE_VA,&entryStage,4)&&runtime.ReadMemory(id,ENTRY_RESULT_VA,&entryResult,4),"real startup and cleanup state");
        printf((char*)"IOSTREAM CASE");Value(" mode=",mode);Value(" id=",id);Value(" checks=",record.checks);Value(" stage=",record.stage);
        Value(" preinit=",record.preinit);Value(" events=",record.raw_calls);Value(" global=",record.directory);
        Value(" cout_sync=",record.cout_sync);Value(" clog_sync=",record.clog_sync);Value(" closed_bad=",record.closed_failure);
        Value(" entry_stage=",entryStage);Value(" entry_result=",entryResult);Value(" exit=",status.exitCode);Value(" vector=",status.faultVector);
        Value(" cs=",status.observedCs);Value(" cr3=",status.observedCr3);Value(" kernel=",kernelDirectory);
        Value(" calls=",callCount[id]);Value(" mask=",callMask[id]);Value(" free=",frames.getStatistics().freeFrames);Value(" baseline=",initial);Value(" open=",store->OpenCount());Value(" error=",record.error);Value(" ack=",record.ack);printf((char*)"\n");
        FileNeed(record.mode==mode&&!record.error&&record.checks>=18&&record.preinit==1,"real pre-main and stream checks");
        FileNeed(record.helper_calls==1,"real platform helpers completed in root");
        FileNeed(record.debug_shared_calls==1&&record.debug_closed==(mode==2?1U:0U),"real debug/shared and closed-stderr cases completed");
        printf((char*)"DEBUG SHARED CASE");Value(" mode=",mode);Value(" calls=",record.debug_shared_calls);Value(" closed=",record.debug_closed);printf((char*)"\n");
        printf((char*)"HELPER CASE");Value(" mode=",mode);Value(" calls=",record.helper_calls);printf((char*)"\n");
        FileNeed(record.temporary_checks==1&&record.temporary_live==1&&record.temporary_heap_files>0&&record.temporary_heap_files<64
            &&temporaryOpens[id]==78+record.temporary_heap_files,"actual temporary files, heap and handle boundaries");
        printf((char*)"TEMPORARY HEAP");Value(" mode=",mode);Value(" files=",record.temporary_heap_files);printf((char*)"\n");
        FileNeed(record.png_bytes>=30,"real byte and wide C entry point boundary checks");
        FileNeed(status.observedCs==0x23&&status.observedCr3==status.directory&&status.directory!=kernelDirectory&&!status.faultVector,"actual CPL3 ownership without fault");
        FileNeed(record.base==0x80000000&&record.vm_handle&&record.foreign==1,"real process heap observed");
        if (normal) {
            const uint32_t result=mode==0?17U:29U;
            FileNeed(record.stage==2&&record.raw_calls==7&&record.directory==2&&record.cout_sync==1&&record.clog_sync==1,"TLS C global and real standard-stream destructor order");
            FileNeed(entryStage==4&&entryResult==result&&status.exitCode==result&&!record.closed_failure&&!store->OpenCount(),"normal return or C exit closes real streams");
        } else {
            FileNeed(record.stage==7&&record.raw_calls==0&&record.directory==1&&!record.cout_sync&&!record.clog_sync,"abnormal exit omits normal destructors");
            FileNeed(entryStage==2&&status.exitCode==(mode==2?33U:0x48000021U)&&record.closed_failure==(mode==2?1U:0U),"closed-stream badbit or actual abort");
            FileNeed(store->OpenCount()==(mode==2?1U:3U),"retained abnormal handles after explicit stdout/stderr close");
        }
        FileNeed(!status.live&&!status.reaped&&files.Statistics().reclaims==before.reclaims,"stopped owner awaits safe Reap");
        FileNeed(runtime.Reap()==1,"one exact safe Reap");
        const NativeFileStatistics after=files.Statistics();const NativeFpStatistics fpAfter=runtime.FpStatistics();
        FileNeed(frames.getStatistics().freeFrames==initial&&!store->OpenCount()&&after.released==before.released+(normal?0U:mode==2?1U:3U)&&after.reclaimFailures==before.reclaimFailures,"all physical frames and handles recovered");
        FileNeed(fpAfter.initialized==fpBefore.initialized+1&&fpAfter.invalidated==fpBefore.invalidated+1&&!fpAfter.invariantFailures,"FP owner recovered");
        Contents("/native/io-out.txt",normal?"002a:1.25\nW\xce\xa9\nMTCG":mode==2?"002a:1.25\nW\xce\xa9\nM":"002a:1.25\nW\xce\xa9\n");
        Contents("/native/io-err.txt",normal?"ER\xce\xa9Lg":mode==2?"ER\xce\xa9L":"ER\xce\xa9");
        const char direct[]={'A',(char)0xce,(char)0xa9,(char)0xf0,(char)0x9f,(char)0x98,(char)0x80,0,'Z'};
        const char partial[]={(char)0xe2,(char)0x82};
        Contents("/native/io-direct.txt",direct,sizeof(direct));Contents("/native/io-invalid.txt",partial,sizeof(partial));
        const char overlong[]={(char)0xf0,(char)0x80,(char)0x80,(char)0x80};
        Contents("/native/io-reject.txt",overlong,sizeof(overlong));
        const unsigned char scalars[]={0x7f,0xc2,0x80,0xdf,0xbf,0xe0,0xa0,0x80,
            0xed,0x9f,0xbf,0xee,0x80,0x80,0xef,0xbf,0xbf,0xf0,0x90,0x80,0x80,0xf4,0x8f,0xbf,0xbf};
        Contents("/native/io-scalar.txt",(const char*)scalars,sizeof(scalars));
        WaitTicks(tasks,8);FileNeed(ring0Progress!=ringBefore&&tasks.BootTicks()>bootBefore,"both continuations alive");
        printf((char*)"IOSTREAM REAP");Value(" id=",id);Value(" free=",frames.getStatistics().freeFrames);Value(" baseline=",initial);Value(" open=",store->OpenCount());Value(" failures=",after.reclaimFailures);printf((char*)"\n");
    }
    FileNeed(ioErrorCount==4&&store->Unmount()==0,"one injected disk fault per owner and clean unmount");InterruptGuard guard;
    printf((char*)"IOSTREAM FINAL");Value(" checks=",fileChecks);Value(" free=",frames.getStatistics().freeFrames);Value(" baseline=",initial);Value(" open=",store->OpenCount());Value(" trap_stack=",maxTrapStack);printf((char*)"\n");
    FileFinish(true);
}
