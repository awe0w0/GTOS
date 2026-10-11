#include "kernel_common.h"
#include "fixture.h"
namespace {
    bool Normal(uint32_t mode) { return mode<2; }
    uint32_t Held(uint32_t mode) { return Normal(mode)?2U:3U; }
    void CheckFiles(uint32_t mode) {
        char buffered[]="/native/buffer0",durable[]="/native/durable0";
        buffered[14]=(char)('0'+mode);durable[15]=(char)('0'+mode);
        FileStore::Info info={};
        FileNeed(store->Stat(buffered,info)==0&&info.size==(Normal(mode)?16U:0U),"normal cleanup flushes; immediate/fault/cancel skips user buffer");
        FileNeed(store->Stat(durable,info)==0&&info.size==9,"already committed file survives every stop");
    }
    void Observe(NativeRuntime& runtime,uint32_t id,uint32_t mode,const NativeStatus& status) {
        const FileRecord record=Record(runtime,id);uint32_t stage=0,result=0;
        FileNeed(runtime.ReadMemory(id,record.command,&stage,4)&&runtime.ReadMemory(id,record.result_address,&result,4),"real integration-entry state retained");
        FileNeed(record.mode==mode&&!record.error&&record.stage==3&&record.mask==1&&record.foreign==id&&record.base==0x80000000&&record.vm_handle,"real initialized consumer and ID");
        const uint32_t markers=Normal(mode)?1:0;
        FileNeed(record.feature==(mode==0?FixtureAdler:FixturePrefixAdler)&&record.read_count>0,"actual codec bytes and checksum consumed");
        FileNeed(mode==0?(record.codec_result==0xfffffffcU&&record.codec_calls==5&&record.codec_frees==4
            &&record.codec_bytes==74164&&record.codec_failed==65536&&record.codec_before==record.codec_after
            &&record.codec_before>32768&&record.codec_before<=65504&&!(record.codec_before&15)):
            !(record.codec_result||record.codec_calls||record.codec_bytes||record.codec_failed||record.codec_frees
                ||record.codec_before||record.codec_after),"actual Chromium compressor heap limit and partial-allocation rollback");
        FileNeed(record.raw_calls==markers&&record.bad_calls==markers&&record.ack==markers,"actual automatic/TLS/fini destructors execute only on normal return");
        FileNeed(stage==(Normal(mode)?4U:2U)&&result==(Normal(mode)?17U:0xffffffffU),"immediate/fault bypasses actual native entry cleanup");
        FileNeed(status.observedCs==0x23&&status.observedCr3==status.directory&&status.directory!=kernelDirectory,"actual CPL3/private CR3");
        const bool fault=(mode==2||mode==3);
        const uint32_t exit=Normal(mode)?17:mode==4?29:mode==5?73:0x8000000e;
        FileNeed(!status.live&&!status.reaped&&status.exitCode==exit&&status.faultVector==(fault?14U:0U),"exact actual exit or explicit unsupported-service fault");
        FileNeed(status.faultError==(fault?(mode==2?6U:7U):0U)&&status.faultAddress==(fault?record.fault_address:0U),"actual nonpresent/RO write fault boundary");
        printf((char*)"NATIVE ZLIB CASE");Value(" feature=",record.feature);Value(" reads=",record.read_count);Value(" fault_error=",status.faultError);Value(" fault_address=",status.faultAddress);Value(" mode=",mode);Value(" id=",id);Value(" auto=",record.raw_calls);Value(" tls=",record.bad_calls);Value(" fini=",record.ack);
        Value(" entry=",stage);Value(" result=",result);Value(" exit=",status.exitCode);Value(" vector=",status.faultVector);Value(" cs=",status.observedCs);Value(" cr3=",status.directory);
        Value(" codec_result=",record.codec_result);Value(" codec_calls=",record.codec_calls);Value(" codec_bytes=",record.codec_bytes);
        Value(" codec_failed=",record.codec_failed);Value(" codec_frees=",record.codec_frees);
        Value(" codec_before=",record.codec_before);Value(" codec_after=",record.codec_after);printf((char*)"\n");
    }
}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"NATIVE ZLIB BOOT");Value(" feature=",FixturePrefixAdler);printf((char*)"\n");
    ObservedDisk bootDisk;FileStore bootStore;disk=&bootDisk;store=&bootStore;
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;fileFrames=&frames;
    FileNeed(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical allocator");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    FileNeed(paging.prepareIdentity(frames,config),"kernel template");
    FileNeed(disk->Identify()&&disk->SectorCount()==2064,"isolated ATA geometry");
    FileNeed(store->Format(disk,8,2048)==0&&store->Mount(disk,8,2048)==0&&store->MakeDirectory("/native")==0,"explicit fresh test volume");
    const char* fixturePaths[]={"/native/input.gz","/native/truncated.gz","/native/corrupt.gz","/native/append.gz"};
    for (uint32_t n=0;n<4;n++) {
        const int32_t handle=store->Open(0xeeeeU,fixturePaths[n],FileStore::Write|FileStore::Create|FileStore::Truncate);
        const uint32_t bytes=sizeof(FixtureGzip)-(n==1||n==3?4U:0U);
        FileNeed(handle>0&&store->WriteFile(0xeeeeU,(uint32_t)handle,n==2?FixtureCorrupt:FixtureGzip,bytes)==(int32_t)bytes
            &&store->Close(0xeeeeU,(uint32_t)handle)==0,"actual compressed fixture stored and closed");
    }
    NativeFiles files(*store);NativeRuntime runtime;
    FileNeed(runtime.AttachFiles(files)&&runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors(),"file attachment and guarded paging");
    FileNeed(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2)&&runtime.FpEnabled()&&runtime.FpError()==NativeFpOk,"FP activation");
    kernelDirectory=paging.getStatistics().directoryAddress;const uint32_t initial=frames.getStatistics().freeFrames;
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;FileNeed((boot->flags&8)&&boot->moduleCount==6,"six real entry/codec/native file modes");
    const MultibootModule* modules=(const MultibootModule*)boot->modules;
    Task ring0(&gdt,Ring0Task);FileNeed(tasks.AddTask(&ring0),"ring0 peer");FileTap tap(interrupts,runtime,tasks,syscalls);
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate();WaitTicks(tasks,8);
    Elf32LoadPlan peerPlan={};const uint32_t peer=Admit(runtime,modules[5],peerPlan),peerCost=Cost(peerPlan)+17;
    Wait(runtime,tasks,peer,3,false);FileNeed(initial-frames.getStatistics().freeFrames==peerCost&&store->OpenCount()==3,"live FILE/VM peer exact baseline");
    peerId=peer;foreignHandle=Record(runtime,peer).gzip_handle;FileNeed(foreignHandle>0,"real live peer gzip handle");
    const uint32_t baseline=frames.getStatistics().freeFrames;
    for (uint32_t mode=0;mode<5;mode++) {
        const uint32_t ringBefore=ring0Progress,bootBefore=tasks.BootTicks(),peerBefore=Record(runtime,peer).progress;
        const NativeFileStatistics before=files.Statistics();const NativeFpStatistics fpBefore=runtime.FpStatistics();
        Elf32LoadPlan plan={};const uint32_t id=Admit(runtime,modules[mode],plan),cost=Cost(plan)+17;
        const NativeStatus status=Wait(runtime,tasks,id,0,true);Observe(runtime,id,mode,status);
        const uint32_t stopped=frames.getStatistics().freeFrames;
        FileNeed(baseline-stopped==cost&&store->OpenCount()==3+Held(mode)&&files.Statistics().released==before.released,"stopped resources retained before Reap");
        FileNeed(runtime.Reap()==1&&frames.getStatistics().freeFrames==baseline&&store->OpenCount()==3,"exact target reclamation preserves live peer");
        const NativeFileStatistics after=files.Statistics();const NativeFpStatistics fpAfter=runtime.FpStatistics();
        FileNeed(after.released==before.released+Held(mode)&&!after.reclaimFailures&&fpAfter.initialized==fpBefore.initialized+1&&fpAfter.invalidated==fpBefore.invalidated+1&&!fpAfter.invariantFailures,"exact file/FP ownership reclaimed");
        CheckFiles(mode);WaitTicks(tasks,8);NativeStatus peerStatus={};
        FileNeed(runtime.Status(peer,peerStatus)&&peerStatus.live&&Record(runtime,peer).progress!=peerBefore&&ring0Progress!=ringBefore&&tasks.BootTicks()>bootBefore,"user peer, ring0 and boot continue after every stop");
        printf((char*)"NATIVE ZLIB REAP");Value(" mode=",mode);Value(" cost=",cost);Value(" held=",baseline-stopped);Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",baseline);Value(" open=",store->OpenCount());Value(" released=",after.released-before.released);
        Value(" peer_before=",peerBefore);Value(" peer_after=",Record(runtime,peer).progress);Value(" ring_before=",ringBefore);Value(" ring_after=",ring0Progress);Value(" boot_before=",bootBefore);Value(" boot_after=",tasks.BootTicks());printf((char*)"\n");
    }
    FileNeed(runtime.RequestExit(peer,73),"cancel actual live peer");const NativeStatus peerStatus=Wait(runtime,tasks,peer,0,true);Observe(runtime,peer,5,peerStatus);
    FileNeed(runtime.Reap()==1&&frames.getStatistics().freeFrames==initial&&!store->OpenCount()&&!files.Statistics().reclaimFailures,"all resources returned to exact initial baseline");
    CheckFiles(5);const NativeFpStatistics fp=runtime.FpStatistics();FileNeed(fp.initialized==6&&fp.invalidated==6&&!fp.invariantFailures,"all FP owners exactly reclaimed");
    FileNeed(ioErrorCount==1,"new backend error reaches actual disk read and LLVM strerror path");
    FileNeed(store->Unmount()==0,"clean isolated test volume unmount");
    InterruptGuard guard;printf((char*)"NATIVE ZLIB FINAL");Value(" checks=",fileChecks);Value(" free=",frames.getStatistics().freeFrames);Value(" expected=",initial);Value(" open=",store->OpenCount());Value(" released=",files.Statistics().released);Value(" trap_stack=",maxTrapStack);printf((char*)"\n");FileFinish(true);
}
