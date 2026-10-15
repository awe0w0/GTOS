#ifndef GTOS_NATIVE_FILE_KERNEL_COMMON_H
#define GTOS_NATIVE_FILE_KERNEL_COMMON_H
#include "fixture-helpers.h"
#include <process/native_files.h>
#include <storage/filestore.h>
#include <process/resources.h>
#include <drivers/ata.h>
#include <memory/criticalsection.h>
#include <process/clock_abi.h>
#include "record.h"
using namespace gtos::storage;
using namespace gtos::drivers;
namespace {
    uint32_t fileChecks,ids[16],idCount,kernelDirectory,callCount[32],callMask[32],badCount[32],maxTrapStack,ioErrorCount;
    PhysicalMemoryManager* fileFrames;
    void FileFinish(bool pass) __attribute__((noreturn));
    void FileFinish(bool pass) {
        printf(pass?(char*)"NATIVE EXIT GUEST PASS\n":(char*)"NATIVE EXIT GUEST FAIL\n");
        asm volatile("outl %0,%1"::"a"(pass?0x10U:0x20U),"Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void FileNeed(bool pass,const char* why) {
        fileChecks++;
        if (!pass) { asm volatile("cli":::"memory");printf((char*)"FAILED FILE ");printf((char*)why);printf((char*)"\n");FileFinish(false); }
    }
    uint32_t Directory() { uint32_t x;asm volatile("mov %%cr3,%0":"=r"(x));return x; }
    uint32_t Flags() { uint32_t x;asm volatile("pushfl; popl %0":"=r"(x));return x; }
    void Value(const char* name,uint32_t x) { printf((char*)name);printfHex32(x); }
    class ObservedDisk:public BlockDevice {
        AdvancedTechnologyAttachment device;
    public:
        uint32_t reads,writes,flushes;bool failRead,failWrite,failFlush;
        ObservedDisk():device(0x1f0,true),reads(0),writes(0),flushes(0),failRead(false),failWrite(false),failFlush(false) {}
        void Stack() {
            uint32_t sp;asm volatile("mov %%esp,%0":"=r"(sp));
            if (sp>=NativeRuntime::KernelStackArena&&sp<NativeRuntime::KernelStackArena+0x15000) {
                const uint32_t index=(sp-NativeRuntime::KernelStackArena)/(5*4096);
                const uint32_t top=NativeRuntime::KernelStackArena+(index+1)*5*4096;
                const uint32_t used=top-sp;if (used>maxTrapStack) maxTrapStack=used;
                FileNeed(used<4*4096-512,"actual ATA stays inside fixed trap stack");
            }
        }
        bool Identify() { return device.Identify(); }
        uint32_t SectorCount() const { return device.SectorCount(); }
        bool ReadSector(uint32_t s,uint8_t* p) { Stack();reads++;return !failRead&&device.ReadSector(s,p); }
        bool WriteSector(uint32_t s,const uint8_t* p) { Stack();writes++;return !failWrite&&device.WriteSector(s,p); }
        bool Flush() { Stack();flushes++;return !failFlush&&device.Flush(); }
    };
    ObservedDisk* disk;
    FileStore* store;
    FileRecord Record(NativeRuntime& r,uint32_t id) {
        FileRecord x={};FileNeed(r.ReadMemory(id,FILE_RECORD_VA,&x,sizeof(x)),"actual user file record");return x;
    }
#include "temporary-observer.h"
    class FileTap:public InterruptHandler {
        NativeRuntime& runtime;TaskManager& tasks;SyscallHandler& original;
    public:
        FileTap(InterruptsManager& i,NativeRuntime& r,TaskManager& t,SyscallHandler& o):InterruptHandler(&i,0x80),runtime(r),tasks(t),original(o) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            CPUState* cpu=(CPUState*)esp;const uint32_t operation=cpu->eax;
            if (operation<0x4720||operation>0x472f) return original.HandlerInterrupt(esp);
            FileNeed(!(Flags()&0x200)&&cpu->cs==0x23,"real IF-clear CPL3 file trap");
            const CPUState before=*cpu;Task* current=tasks.CurrentTask();const uint32_t cr3=Directory();
            FileNeed(current&&current->UserMode()&&cr3!=kernelDirectory,"real user task/private CR3");
            uint32_t id=0;NativeStatus initial={};
            for (uint32_t n=0;n<idCount;n++) {
                NativeStatus s={};if (runtime.Status(ids[n],s)&&s.live&&s.directory==cr3) { id=ids[n];initial=s; }
            }
            FileNeed(id&&id<32,"admitted owner derived from current CR3");
            // The management ReadMemory API deliberately requires kernel CR3.
            // This trap observer reads only its fixed, admitted, one-page record
            // through the actual current mapping, after independent PDE/PTE checks.
            const uint32_t pde=((const uint32_t*)cr3)[FILE_RECORD_VA>>22];
            FileNeed((pde&7)==7&&!(pde&0x80),"trap record private PDE");
            const uint32_t pte=((const uint32_t*)(pde&~4095U))[(FILE_RECORD_VA>>12)&1023];
            FileNeed((pte&7)==7&&fileFrames->isAllocated(pte&~4095U),"trap record owned RW PTE");
            const FileRecord record=*(const FileRecord*)FILE_RECORD_VA;
            const uint32_t free=fileFrames->getStatistics().freeFrames,count=tasks.TaskCount(),ticks=tasks.Ticks();
            const uint32_t reads=disk->reads,writes=disk->writes,flushes=disk->flushes,open=store->OpenCount();
            const uint32_t command=record.command;
            const uint32_t injection=(command==1&&operation==GTOS_SYS_FILE_READ)?1U:
                (command==2&&(operation==GTOS_SYS_FILE_WRITE||operation==GTOS_SYS_FILE_SYNC))?2U:
                (command==3&&operation==GTOS_SYS_FILE_CLOSE)?3U:
                (command==4&&operation==GTOS_SYS_FILE_OPEN)?2U:0U;
            disk->failRead=injection==1;disk->failWrite=injection==2;disk->failFlush=injection==3;
            const bool protect=command==5&&operation==GTOS_SYS_FILE_OPEN;
            if (protect) FileNeed(!open&&store->Unmount()==0&&store->Mount(disk,8,2048,true)==0,"mount actual volume readonly without live handles");
            const uint32_t returned=original.HandlerInterrupt(esp);NativeStatus observed={};
            if (protect) {
                FileNeed((int32_t)cpu->eax==FileStore::ReadOnly&&!store->OpenCount()
                    &&disk->writes==writes&&disk->flushes==flushes,"actual readonly temporary open has no write or handle");
                FileNeed(store->Unmount()==0&&store->Mount(disk,8,2048)==0,"restore writable volume");
            }
            disk->failRead=false;disk->failWrite=false;disk->failFlush=false;
            ObserveTemporary(id,cr3,before,(int32_t)cpu->eax);
            if (command==4&&operation==GTOS_SYS_FILE_OPEN) FileNeed((int32_t)cpu->eax==LFS_ERR_IO && store->OpenCount()==open,"temporary open IO error leaves no handle");
            if (injection&&(int32_t)cpu->eax==LFS_ERR_IO) {
                FileNeed(injection==1?disk->reads>reads:injection==2?disk->writes>writes:disk->flushes>flushes,"error reaches actual disk callback");
                ioErrorCount++;
            }
            FileNeed(returned==esp&&tasks.CurrentTask()==current&&Directory()==cr3&&tasks.Ticks()==ticks
                &&tasks.TaskCount()==(int)count&&fileFrames->getStatistics().freeFrames==free,"file call preserves scheduler and VM ownership");
            FileNeed(runtime.Status(id,observed)&&observed.systemCalls==initial.systemCalls+1&&observed.observedCs==0x23
                &&observed.observedCr3==cr3,"runtime records authoritative owner");
            FileNeed(cpu->ebx==before.ebx&&cpu->ecx==before.ecx&&cpu->edx==before.edx
                &&cpu->esi==before.esi&&cpu->edi==before.edi&&cpu->ebp==before.ebp
                &&cpu->ds==before.ds&&cpu->es==before.es&&cpu->fs==before.fs&&cpu->gs==before.gs
                &&cpu->eip==before.eip&&cpu->cs==before.cs&&cpu->esp==before.esp&&cpu->ss==before.ss
                &&(cpu->eflags&0x3302)==0x202,"all non-result registers/segments preserved");
            callCount[id]++;callMask[id]|=1U<<(operation-0x4720);
            if (record.invalid_io) {
                FileNeed((int32_t)cpu->eax<0&&disk->reads==reads&&disk->writes==writes&&disk->flushes==flushes
                    &&store->OpenCount()==open,"validation rejects before all ATA IO and handles");
                badCount[id]++;
            }
            return returned;
        }
    };
    uint32_t Admit(NativeRuntime& runtime,const MultibootModule& module,Elf32LoadPlan& plan) {
        InterruptGuard guard;
        FileNeed(module.end>module.start&&ValidateElf32((const uint8_t*)module.start,module.end-module.start,plan),"real bounded ELF32 admission");
        uint32_t id;FileNeed(runtime.CreateElf((const uint8_t*)module.start,module.end-module.start,id),"CreateElf");
        FileNeed(id&&id<32&&idCount<16,"bounded independent owner IDs");
        for (uint32_t i=0;i<idCount;i++) FileNeed(ids[i]!=id,"owner IDs never reused");
        ids[idCount++]=id;return id;
    }
    NativeStatus Wait(NativeRuntime& runtime,TaskManager& tasks,uint32_t id,uint32_t stage,bool stop) {
        const uint32_t begin=tasks.Ticks();
        for (;;) {
            NativeStatus status={};FileRecord record;
            { InterruptGuard guard;FileNeed(runtime.Status(id,status),"live/retained status");record=Record(runtime,id); }
            if (record.error||(!status.live&&status.exitCode==91)) {
                printf((char*)"EXIT USER ERROR");Value(" stage=",record.stage);Value(" error=",record.error);Value(" checks=",record.checks);
                Value(" line=",record.ack);Value(" exit=",status.exitCode);Value(" vector=",status.faultVector);
                Value(" detail_return=",record.mask);Value(" detail_errno=",record.foreign);
                Value(" detail_error_bit=",record.bad_calls);printf((char*)"\n");FileFinish(false);
            }
            if (stop?!status.live:record.stage==stage) return status;
            FileNeed(!record.error&&status.live&&tasks.Ticks()-begin<5000,"bounded actual file suite/continuation");WaitTicks(tasks,1);
        }
    }

    void TemporaryStoppedCase(NativeRuntime& runtime,TaskManager& tasks,NativeFiles& files,uint32_t id,uint32_t mode,
                             uint32_t baseline,const NativeFileStatistics& before,const NativeFpStatistics& fpBefore) {
        if (mode==5) {
            NativeStatus ready=Wait(runtime,tasks,id,7,false);
            FileNeed(ready.live&&runtime.RequestExit(id,0x43)&&!runtime.RequestExit(id,0x43),"cancel live owner once from safe kernel context");
        }
        const NativeStatus status=Wait(runtime,tasks,id,0,true);const FileRecord record=Record(runtime,id);
        FileNeed(record.mode==mode&&record.stage==7&&!record.error&&record.preinit==1&&record.temporary_live==1
            &&temporaryOpens[id]==1&&store->OpenCount()==1,"stopped process retains one real unnamed file");
        FileNeed(status.exitCode==(mode==4?0x80000006U:0x43U)&&status.faultVector==(mode==4?6U:0U)
            &&status.observedCs==0x23&&status.observedCr3==status.directory&&status.directory!=kernelDirectory,"actual UD2 fault or external cancellation");
        FileNeed(!status.live&&!status.reaped&&files.Statistics().reclaims==before.reclaims&&runtime.Reap()==1,"one safe stopped-owner Reap");
        const NativeFileStatistics after=files.Statistics();const NativeFpStatistics fpAfter=runtime.FpStatistics();
        FileNeed(fileFrames->getStatistics().freeFrames==baseline&&!store->OpenCount()
            &&after.released==before.released+1&&after.reclaimFailures==before.reclaimFailures,"fault/cancel recovers exact file and physical pages");
        FileNeed(fpAfter.initialized==fpBefore.initialized+1&&fpAfter.invalidated==fpBefore.invalidated+1
            &&fpAfter.invariantFailures==fpBefore.invariantFailures,"fault/cancel recovers FP owner");
        printf((char*)"TEMPORARY STOP");Value(" mode=",mode);Value(" exit=",status.exitCode);Value(" vector=",status.faultVector);
        Value(" free=",fileFrames->getStatistics().freeFrames);Value(" baseline=",baseline);Value(" open=",store->OpenCount());printf((char*)"\n");
    }
}
asm(".section .text.native_file_unused_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");

#endif
