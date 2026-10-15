#include "kernel_common.h"
#include "entry-symbols.h"
namespace {
    void ThreadContents(const char* path) {
        int32_t file=store->Open(0xf0000000,path,FileStore::Read);uint8_t bytes[32];uint32_t seen=0;
        FileNeed(file>0&&store->ReadFile(0xf0000000,file,bytes,sizeof(bytes))==24&&store->Size(0xf0000000,file)==24,"three committed eight-byte thread writes");
        for (uint32_t n=0;n<3;n++) {
            const uint8_t* token=bytes+n*8;FileNeed(token[0]>='0'&&token[0]<='2',"actual thread token");
            const uint32_t bit=1U<<(token[0]-'0');FileNeed(!(seen&bit),"each thread wrote once");seen|=bit;
            FileNeed(token[1]==':'&&token[2]==0&&token[3]=='G'&&token[4]=='T'&&token[5]=='O'&&token[6]=='S'&&token[7]=='\n',"complete locked writes including NUL");
        }
        FileNeed(seen==7&&store->Close(0xf0000000,file)==0&&!store->OpenCount(),"independent thread-output reader released");
    }
    void Contents(const char* path,const char* expected,uint32_t length=0) {
        int32_t file=store->Open(0xf0000000,path,FileStore::Read);
        FileNeed(file>0,"closed stream readable by independent owner");
        uint8_t bytes[64];int32_t count=store->ReadFile(0xf0000000,file,bytes,sizeof(bytes));
        if (!length) while (expected[length]) length++;
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
    FileNeed(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2,true)&&runtime.FpEnabled()&&runtime.FpError()==NativeFpOk,"real four-context SSE2 activation");
    kernelDirectory=paging.getStatistics().directoryAddress;const uint32_t initial=frames.getStatistics().freeFrames;
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;FileNeed((boot->flags&8)&&boot->moduleCount==1,"one real entry and standard-stream ELF");
    const MultibootModule* modules=(const MultibootModule*)boot->modules;
    Task ring0(&gdt,Ring0Task);FileNeed(tasks.AddTask(&ring0),"ring0 peer");FileTap tap(interrupts,runtime,tasks,syscalls);
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate();WaitTicks(tasks,8);
    for (uint32_t mode=0;mode<6;mode++) {
        for (uint32_t n=0;n<3;n++) workerFileCalls[n]=0;
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
        Value(" calls=",callCount[id]);Value(" mask=",callMask[id]);Value(" free=",frames.getStatistics().freeFrames);Value(" baseline=",initial);Value(" open=",store->OpenCount());printf((char*)"\n");
        FileNeed(record.mode==mode&&!record.error&&record.checks>=18&&record.preinit==1,"real pre-main and stream checks");
        FileNeed(record.temporary_checks==1&&record.temporary_live==1&&record.temporary_heap_files>0&&record.temporary_heap_files<64
            &&temporaryOpens[id]==84+record.temporary_heap_files,"actual temporary files, heap/handle boundaries and per-thread errno");
        printf((char*)"TEMPORARY HEAP");Value(" mode=",mode);Value(" files=",record.temporary_heap_files);printf((char*)"\n");
        FileNeed(record.png_bytes>=30,"real byte and wide C entry point boundary checks");
        FileNeed(record.thread_profile==4&&record.thread_file_done==3&&record.thread_stdout_done==3
            &&record.thread_mutex_done==3&&record.thread_recursive_done==3&&record.thread_joins==3
            &&record.thread_blocked==8,"actual locked FILE stdout mutex and recursive waits");
        FileNeed(record.thread_once_enter==3&&record.thread_once_done==3&&record.thread_once_calls==1
            &&record.thread_external_enter==3&&record.thread_external_done==3&&record.thread_external_calls==1,"one actual callback and all waiting returns");
        FileNeed(record.thread_notify_done==3&&record.thread_signal_wakes==1&&record.thread_broadcast_wakes==3,"one selected waiter then remaining broadcast");
        for (uint32_t n=0;n<3;n++) {
            NativeStatus worker={};FileNeed(record.thread_ids[n]&&record.thread_ids[n]!=id&&record.thread_stage[n]==5
                &&runtime.Status(record.thread_ids[n],worker)&&!worker.live&&worker.reaped&&!worker.exitCode&&!worker.faultVector
                &&worker.directory==status.directory&&worker.observedCr3==status.directory&&worker.observedCs==0x23
                &&worker.statistics.yields>0&&workerFileCalls[n]>=2,"real worker stopped joined and performed file calls");
            for (uint32_t j=n+1;j<3;j++) FileNeed(record.thread_ids[n]!=record.thread_ids[j],"three independent thread IDs");
        }
        printf((char*)"IOSTREAM THREAD CASE");Value(" mode=",mode);Value(" profile=",record.thread_profile);Value(" joins=",record.thread_joins);
        Value(" blocked=",record.thread_blocked);Value(" once=",record.thread_once_calls);Value(" external=",record.thread_external_calls);
        Value(" signal=",record.thread_signal_wakes);Value(" broadcast=",record.thread_broadcast_wakes);
        const char* ownerNames[]={" worker0="," worker1="," worker2="};
        const char* callNames[]={" calls0="," calls1="," calls2="};
        for (uint32_t n=0;n<3;n++) { Value(ownerNames[n],record.thread_ids[n]);Value(callNames[n],workerFileCalls[n]); }printf((char*)"\n");
        FileNeed(status.observedCs==0x23&&status.observedCr3==status.directory&&status.directory!=kernelDirectory&&!status.faultVector,"actual CPL3 ownership without fault");
        FileNeed(record.base==0x80000000&&record.vm_handle&&record.foreign==1,"real process heap observed");
        if (normal) {
            const uint32_t result=mode==0?17U:29U;
            FileNeed(record.stage==2&&record.raw_calls==7&&record.directory==2&&record.cout_sync==1&&record.clog_sync==1,"TLS C global and real standard-stream destructor order");
            FileNeed(entryStage==4&&entryResult==result&&status.exitCode==result&&!record.closed_failure&&!store->OpenCount(),"normal return or C exit closes real streams");
        } else {
            FileNeed(record.stage==7&&record.raw_calls==0&&record.directory==1&&!record.cout_sync&&!record.clog_sync,"abnormal exit omits normal destructors");
            FileNeed(entryStage==2&&status.exitCode==(mode==2?33U:0x48000021U)&&record.closed_failure==(mode==2?1U:0U),"closed-stream badbit or actual abort");
            FileNeed(store->OpenCount()==(mode==2?2U:3U),"retained abnormal handles including unnamed temporary");
        }
        FileNeed(!status.live&&!status.reaped&&files.Statistics().reclaims==before.reclaims,"stopped owner awaits safe Reap");
        FileNeed(runtime.Reap()==1,"one exact safe Reap");
        const NativeFileStatistics after=files.Statistics();const NativeFpStatistics fpAfter=runtime.FpStatistics();
        FileNeed(frames.getStatistics().freeFrames==initial&&!store->OpenCount()&&after.released==before.released+(normal?0U:mode==2?2U:3U)&&after.reclaimFailures==before.reclaimFailures,"all physical frames and handles recovered");
        FileNeed(fpAfter.initialized==fpBefore.initialized+4&&fpAfter.invalidated==fpBefore.invalidated+4&&!fpAfter.invariantFailures,"all four FP owners recovered");
        ThreadContents("/native/io-thread-file.txt");ThreadContents("/native/io-thread-out.txt");
        Contents("/native/io-out.txt",normal?"002a:1.25\nW\xce\xa9\nMTCG":mode==2?"002a:1.25\nW\xce\xa9\nM":"002a:1.25\nW\xce\xa9\n");
        Contents("/native/io-err.txt",normal?"ER\xce\xa9Lg":"ER\xce\xa9");
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
