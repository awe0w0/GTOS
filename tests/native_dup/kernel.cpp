#include <process/native_runtime.h>
#include <process/native_files.h>
#include <storage/filestore.h>
#include <drivers/ata.h>
#include <hardwarecommunication/interrupts.h>
#include <syscalls.h>
#include <memory/criticalsection.h>
#include "record.h"
using namespace gtos;
using namespace gtos::process;
using namespace gtos::storage;
using namespace gtos::drivers;
using namespace gtos::memory;
using namespace gtos::hardwarecommunication;
extern "C" uint8_t kernel_start,kernel_end,kernel_readonly_start,kernel_readonly_end;
void printf(char* text) {while (*text) {asm volatile("outb %0,$0xe9"::"a"(*text));++text;}}
void printfHex32(uint32_t value) {for (int shift=28;shift>=0;shift-=4) {char c="0123456789ABCDEF"[(value>>shift)&15];asm volatile("outb %0,$0xe9"::"a"(c));}}
void printfHex(uint8_t value) {printfHex32(value);}
void operator delete(void*) {}
namespace {
    uint8_t image[65536];
    uint32_t irqs,calls,successes,errors,peak;
    void Finish(bool pass) __attribute__((noreturn));
    void Finish(bool pass) {
        printf(pass?(char*)"CANONICAL DUP GUEST PASS\n":(char*)"CANONICAL DUP GUEST FAIL\n");
        asm volatile("outl %0,%1"::"a"(pass?0x10U:0x20U),"Nd"((uint16_t)0xf4));for (;;) asm volatile("cli; hlt");
    }
    void Need(bool value,const char* why) {if (!value) {printf((char*)"FAILED DUP ");printf((char*)why);printf((char*)"\n");Finish(false);}}
    uint32_t Flags() {uint32_t value;asm volatile("pushfl; popl %0":"=r"(value));return value;}
    void Wait(TaskManager& tasks,uint32_t ticks) {uint32_t before=tasks.Ticks();while ((uint32_t)(tasks.Ticks()-before)<ticks) asm volatile("sti; hlt":::"memory");}
    uint32_t Get32(const uint8_t* p) {return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
    void Put32(uint8_t* p,uint32_t value) {for (unsigned i=0;i<4;++i) p[i]=value>>(8*i);}
    CpuGuestRecord Record(NativeRuntime& runtime,uint32_t id) {CpuGuestRecord record={};Need(runtime.ReadMemory(id,CPU_RECORD_VA,&record,sizeof(record)),"retained actual user record");return record;}
    uint32_t Create(NativeRuntime& runtime,const MultibootModule& module,uint32_t mode,uint32_t peerFile=0) {
        Need(module.end>module.start&&module.end-module.start<=sizeof(image),"bounded raw ELF");uint32_t size=module.end-module.start;
        for (unsigned i=0;i<size;++i) image[i]=((const uint8_t*)module.start)[i];
        Elf32LoadPlan plan;Need(ValidateElf32(image,size,plan),"real ELF validation");bool found=false;
        for (unsigned i=0;i<plan.segmentCount;++i) {const Elf32LoadSegment& segment=plan.segments[i];if (segment.virtualAddress!=CPU_RECORD_VA) continue;
            Need(!found&&(segment.flags&Elf32Write)&&!(segment.flags&Elf32Execute)&&segment.fileSize>=sizeof(CpuGuestRecord)
                &&Get32(image+segment.fileOffset)==1&&Get32(image+segment.fileOffset+4)==0,"initialized writable record");
            Put32(image+segment.fileOffset+4,mode);Put32(image+segment.fileOffset+__builtin_offsetof(CpuGuestRecord,filePeer),peerFile);found=true;
        }
        Need(found,"only mode/peer handle annotated in load-copy");uint32_t id=0;Need(runtime.CreateElf(image,size,id)&&id,"canonical ELF admission");return id;
    }
    class Disk:public BlockDevice {
        AdvancedTechnologyAttachment device;
    public:
        uint32_t reads,writes,flushes;bool failFlush;
        Disk():device(0x1f0,true),reads(0),writes(0),flushes(0),failFlush(false) {}
        virtual bool Identify() {return device.Identify();}
        virtual uint32_t SectorCount() const {return device.SectorCount();}
        virtual bool ReadSector(uint32_t n,uint8_t* p) {++reads;return device.ReadSector(n,p);}
        virtual bool WriteSector(uint32_t n,const uint8_t* p) {++writes;return device.WriteSector(n,p);}
        virtual bool Flush() {++flushes;return !failFlush&&device.Flush();}
    };
    class Timer:public InterruptHandler {
    public:
        Timer(InterruptsManager& interrupts):InterruptHandler(&interrupts,0x20) {}
        uint32_t HandlerInterrupt(uint32_t esp) {Need(!(Flags()&0x200),"IRQ IF0");++irqs;return esp;}
    };
    class Tap:public InterruptHandler {
        TaskManager& tasks;PhysicalMemoryManager& frames;FileStore& files;Disk& disk;SyscallHandler& original;
    public:
        Tap(InterruptsManager& interrupts,TaskManager& t,PhysicalMemoryManager& f,FileStore& s,Disk& d,SyscallHandler& h)
            :InterruptHandler(&interrupts,0x80),tasks(t),frames(f),files(s),disk(d),original(h) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            CPUState* cpu=(CPUState*)esp;if (cpu->eax!=GTOS_SYS_FILE_DUP) return original.HandlerInterrupt(esp);
            Need((cpu->cs&3)==3&&!(Flags()&0x200),"actual DUP CPL3/IF0");const CPUState before=*cpu;Task* task=tasks.CurrentTask();
            const uint32_t free=frames.getStatistics().freeFrames,open=files.OpenCount(),ticks=irqs,reads=disk.reads,writes=disk.writes,flushes=disk.flushes;
            if (open>peak) peak=open;uint32_t result=original.HandlerInterrupt(esp);++calls;
            Need(result==esp&&tasks.CurrentTask()==task&&frames.getStatistics().freeFrames==free&&irqs==ticks
                &&disk.reads==reads&&disk.writes==writes&&disk.flushes==flushes,"DUP only updates bounded handle metadata");
            Need(cpu->ebx==before.ebx&&cpu->ecx==before.ecx&&cpu->edx==before.edx&&cpu->esi==before.esi&&cpu->edi==before.edi&&cpu->ebp==before.ebp,"other GPRs preserved");
            if ((int32_t)cpu->eax<0) {++errors;Need(files.OpenCount()==open,"rejected DUP is atomic");}
            else {++successes;Need(cpu->eax&&files.OpenCount()==open+1,"one actual new owned handle");}return result;
        }
    };
}
extern "C" __attribute__((noreturn)) void __cxa_pure_virtual() {printf((char*)"FAILED DUP pure virtual dispatch\n");Finish(false);}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"CANONICAL DUP GUEST BOOT\n");FileStore store;Disk disk;
    Need(disk.Identify()&&disk.SectorCount()==2064,"dedicated actual ATA disk");Need(store.Format(&disk,8,2048)==0&&store.Mount(&disk,8,2048)==0,"explicit fresh test-volume format/mount");NativeFiles endpoint(store);
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;Need(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical allocator");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    Need(paging.prepareIdentity(frames,config),"identity template");NativeRuntime runtime;
    Need(runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors()&&runtime.AttachFiles(endpoint),"guard stacks/sealed paging/real files");
    Need(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2,true),"canonical SSE2/four-context runtime");Timer timer(interrupts);Tap tap(interrupts,tasks,frames,store,disk,syscalls);
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;Need((boot->flags&8)&&boot->moduleCount==1,"single raw ELF");const MultibootModule& module=*(const MultibootModule*)boot->modules;
    const uint32_t initial=frames.getStatistics().freeFrames,peer=Create(runtime,module,1),baseline=frames.getStatistics().freeFrames;interrupts.Activate();Wait(tasks,8);
    Need(Record(runtime,peer).filePhase==1&&Record(runtime,peer).fileShared&&store.OpenCount()==1,"independent peer ready");uint32_t peerFile=Record(runtime,peer).fileShared;
    for (unsigned i=0;i<8;++i) {
        const unsigned mode=i==7?2:0,before=Record(runtime,peer).checks,id=Create(runtime,module,mode,peerFile),begin=tasks.Ticks();NativeStatus status={};
        do {Need(runtime.Status(id,status),"status exists");if (!status.live) break;Wait(tasks,1);} while ((uint32_t)(tasks.Ticks()-begin)<500);
        CpuGuestRecord record=Record(runtime,id);Need(!status.live&&status.observedCs==0x23&&status.observedCr3==status.directory&&status.systemCalls&&record.fileChecks,"actual bounded own-CR3 user file execution");
        if (!mode) Need(!status.exitCode&&!status.faultVector&&record.phase==2&&record.joined==3&&record.workerDone==7&&record.filePhase==3,"worker joins/reuse and shared cursor");
        else Need(status.faultVector==14&&status.faultError==6&&status.faultAddress==0xBFFFCFFCU&&status.exitCode==(0x80000000U|14)&&record.workerStarted==3&&record.filePhase==2,"whole-group fault after both file workers");
        unsigned leaked=mode?5:4;Need(record.fileLeaks==leaked&&store.OpenCount()==leaked+1,"exact retained aliases before reap");NativeFileStatistics old=endpoint.Statistics();disk.failFlush=i==5;
        Need(runtime.Reap()==(mode?3U:1U)&&frames.getStatistics().freeFrames==baseline,"exact group reap/peer physical baseline");disk.failFlush=false;NativeFileStatistics now=endpoint.Statistics();
        Need(store.OpenCount()==1&&now.reclaims==old.reclaims+1&&now.released==old.released+leaked&&now.reclaimFailures==old.reclaimFailures+(i==5?1U:0U)&&now.lastReclaimError==(i==5?-5:0),"exact owner cleanup and injected-error report");
        Wait(tasks,8);NativeStatus peerStatus={};Need(runtime.Status(peer,peerStatus)&&peerStatus.live&&Record(runtime,peer).checks>before&&store.Seek(peer,peerFile,0,1)==0&&store.Size(peer,peerFile)==4,"peer file and task survive");
        {InterruptGuard printGuard;printf((char*)"DUP REAP leaked=");printfHex32(leaked);printf((char*)" released=");printfHex32(now.released-old.released);printf((char*)" error=");printfHex32((uint32_t)now.lastReclaimError);printf((char*)" free=");printfHex32(frames.getStatistics().freeFrames);printf((char*)" expected=");printfHex32(baseline);printf((char*)"\n");}
    }
    Need(runtime.RequestExit(peer,0)&&runtime.Reap()==1&&frames.getStatistics().freeFrames==initial&&!store.OpenCount(),"final exact descriptor/physical baseline");NativeFileStatistics fs=endpoint.Statistics();NativeFpStatistics fp=runtime.FpStatistics();
    Need(calls==671&&successes==567&&errors==104&&peak==64&&fs.reclaims==9&&fs.released==34&&fs.reclaimFailures==1&&fp.initialized==32&&fp.invalidated==32&&fp.saves&&fp.restores&&!fp.invariantFailures&&irqs>10,"canonical DUP/FP/IRQ totals");Need(store.Unmount()==0,"all underlying resources closed");
    printf((char*)"DUP FINAL calls=");printfHex32(calls);printf((char*)" success=");printfHex32(successes);printf((char*)" errors=");printfHex32(errors);printf((char*)" peak=");printfHex32(peak);printf((char*)" reclaims=");printfHex32(fs.reclaims);printf((char*)" released=");printfHex32(fs.released);printf((char*)" failures=");printfHex32(fs.reclaimFailures);printf((char*)" initialized=");printfHex32(fp.initialized);printf((char*)" invalidated=");printfHex32(fp.invalidated);printf((char*)" saves=");printfHex32(fp.saves);printf((char*)" restores=");printfHex32(fp.restores);printf((char*)" fp_failures=");printfHex32(fp.invariantFailures);printf((char*)" free=");printfHex32(frames.getStatistics().freeFrames);printf((char*)" expected=");printfHex32(initial);printf((char*)"\n");Finish(true);
}
