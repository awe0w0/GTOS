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
void operator delete(void*) {__builtin_trap();}
namespace {
    uint8_t image[2*1024*1024];
    uint32_t irqs,rootId,rootDirectory,calls,successes,errors,peak,closeErrors,heapOpenCalls;
    void Finish(bool pass) __attribute__((noreturn));
    void Finish(bool pass) {
        printf(pass?(char*)"NATIVE DIRECTORY RUNTIME PASS\n":(char*)"NATIVE DIRECTORY RUNTIME FAIL\n");
        asm volatile("outl %0,%1"::"a"(pass?0x10U:0x20U),"Nd"((uint16_t)0xf4));for (;;) asm volatile("cli; hlt");
    }
    void Need(bool pass,const char* why) {if (!pass) {printf((char*)"FAILED DIRECTORY ");printf((char*)why);printf((char*)"\n");Finish(false);}}
    uint32_t Flags() {uint32_t value;asm volatile("pushfl; popl %0":"=r"(value));return value;}
    uint32_t Cr3() {uint32_t value;asm volatile("mov %%cr3,%0":"=r"(value));return value;}
    void Wait(TaskManager& tasks,uint32_t ticks) {uint32_t before=tasks.Ticks();while ((uint32_t)(tasks.Ticks()-before)<ticks) asm volatile("sti; hlt":::"memory");}
    uint32_t Get32(const uint8_t* p) {return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
    void Put32(uint8_t* p,uint32_t value) {for (unsigned i=0;i<4;++i) p[i]=value>>(8*i);}
    DirectoryRecord Record(NativeRuntime& runtime,uint32_t id) {DirectoryRecord record={};Need(runtime.ReadMemory(id,DIRECTORY_RECORD_VA,&record,sizeof(record)),"actual retained user record");return record;}
    uint32_t Create(NativeRuntime& runtime,const MultibootModule& module,uint32_t mode,uint32_t peerFile=0) {
        Need(module.end>module.start&&module.end-module.start<=sizeof(image),"bounded real runtime ELF");uint32_t size=module.end-module.start;
        for (unsigned i=0;i<size;++i) image[i]=((const uint8_t*)module.start)[i];
        Elf32LoadPlan plan;Need(ValidateElf32(image,size,plan),"ELF32 admission plan");bool found=false;
        for (unsigned i=0;i<plan.segmentCount;++i) {const Elf32LoadSegment& segment=plan.segments[i];if (segment.virtualAddress!=DIRECTORY_RECORD_VA) continue;
            Need(!found&&(segment.flags&Elf32Write)&&!(segment.flags&Elf32Execute)&&segment.fileSize>=sizeof(DirectoryRecord)
                &&Get32(image+segment.fileOffset)==1&&Get32(image+segment.fileOffset+4)==0,"initialized writable record");
            Put32(image+segment.fileOffset+4,mode);Put32(image+segment.fileOffset+__builtin_offsetof(DirectoryRecord,peerFile),peerFile);found=true;
        }
        Need(found,"only mode/foreign descriptor annotated in load copy");uint32_t id=0;Need(runtime.CreateElf(image,size,id)&&id,"canonical native runtime image");return id;
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
            CPUState* cpu=(CPUState*)esp;
            if (rootDirectory&&Cr3()==rootDirectory&&(cpu->eax==GTOS_SYS_FILE_DIR_OPEN||cpu->eax==GTOS_SYS_FILE_CLOSE)) {
                Need((cpu->cs&3)==3&&!(Flags()&0x200)&&tasks.CurrentTask()&&tasks.CurrentTask()->UserMode(),"known root user trap and CR3");
                // This fixed fixture's admitted static data page cannot be
                // unmapped through VM_RELEASE. Read only its three test fields
                // in the currently active root address space, with IF clear.
                const volatile DirectoryRecord& record=*(const volatile DirectoryRecord*)DIRECTORY_RECORD_VA;
                if (cpu->eax==GTOS_SYS_FILE_DIR_OPEN&&record.phase==DirectoryHeapFull) ++heapOpenCalls;
                if (cpu->eax==GTOS_SYS_FILE_CLOSE&&record.command==1) {
                    Need(cpu->ecx==sizeof(GtosFileControlRequest)&&cpu->ebx>=NativeRuntime::UserStackBottom
                        &&cpu->ebx<=NativeRuntime::UserStackTop-sizeof(GtosFileControlRequest),"fixture close request inside admitted two-page root stack");
                    const volatile GtosFileControlRequest& request=*(const volatile GtosFileControlRequest*)cpu->ebx;
                    Need(request.version==1&&request.handle==record.ack,"exact caller-requested final close injection");
                    uint32_t before=files.OpenCount();disk.failFlush=true;uint32_t result=original.HandlerInterrupt(esp);disk.failFlush=false;
                    Need(result==esp&&(int32_t)cpu->eax==-5&&files.OpenCount()+1==before,"actual failing final close releases handle");++closeErrors;return result;
                }
            }
            if (cpu->eax!=GTOS_SYS_FILE_DUP) return original.HandlerInterrupt(esp);
            Need((cpu->cs&3)==3&&!(Flags()&0x200),"actual DUP CPL3/IF0");const CPUState saved=*cpu;Task* task=tasks.CurrentTask();
            const uint32_t free=frames.getStatistics().freeFrames,open=files.OpenCount(),ticks=irqs,reads=disk.reads,writes=disk.writes,flushes=disk.flushes;
            if (open>peak) peak=open;uint32_t result=original.HandlerInterrupt(esp);++calls;
            Need(result==esp&&tasks.CurrentTask()==task&&frames.getStatistics().freeFrames==free&&irqs==ticks&&disk.reads==reads&&disk.writes==writes&&disk.flushes==flushes,"DUP only changes handle metadata");
            Need(cpu->ebx==saved.ebx&&cpu->ecx==saved.ecx&&cpu->edx==saved.edx&&cpu->esi==saved.esi&&cpu->edi==saved.edi&&cpu->ebp==saved.ebp,"other GPRs retained");
            if ((int32_t)cpu->eax<0) {++errors;Need(files.OpenCount()==open,"failed DUP atomic");}
            else {++successes;Need(cpu->eax&&files.OpenCount()==open+1,"one new actual descriptor");}return result;
        }
    };
}
extern "C" __attribute__((noreturn)) void __cxa_pure_virtual() {printf((char*)"FAILED DIRECTORY pure virtual\n");Finish(false);}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"NATIVE DIRECTORY RUNTIME BOOT\n");FileStore store;Disk disk;
    Need(disk.Identify()&&disk.SectorCount()==2064,"dedicated new ATA disk");Need(store.Format(&disk,8,2048)==0&&store.Mount(&disk,8,2048)==0,"explicit test-volume format/mount");NativeFiles endpoint(store);
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;Need(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical allocator");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    Need(paging.prepareIdentity(frames,config),"identity map");NativeRuntime runtime;
    Need(runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors()&&runtime.AttachFiles(endpoint),"guard stacks/real files/sealed paging");
    Need(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2,true),"canonical SSE2 four-context runtime");Timer timer(interrupts);Tap tap(interrupts,tasks,frames,store,disk,syscalls);
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;Need((boot->flags&8)&&boot->moduleCount==1,"one actual runtime ELF");const MultibootModule& module=*(const MultibootModule*)boot->modules;
    const uint32_t initial=frames.getStatistics().freeFrames,peer=Create(runtime,module,1),admitted=frames.getStatistics().freeFrames;interrupts.Activate();
    uint32_t peerBegan=tasks.Ticks();
    while (Record(runtime,peer).phase!=DirectoryRetained) {NativeStatus pending={};Need(runtime.Status(peer,pending)&&pending.live&&(uint32_t)(tasks.Ticks()-peerBegan)<1500,"bounded real FILE peer startup");Wait(tasks,1);}
    DirectoryRecord peerRecord=Record(runtime,peer);Need(peerRecord.phase==DirectoryRetained&&peerRecord.peerFile&&store.OpenCount()==1,"actual C FILE peer ready");
    // fopen retains its actual malloc arena. Establish the peer baseline only
    // after that one-time startup, before admitting any tested root process.
    const uint32_t baseline=frames.getStatistics().freeFrames;
    {InterruptGuard printGuard;printf((char*)"DIRECTORY BASELINE initial=");printfHex32(initial);printf((char*)" admitted=");printfHex32(admitted);printf((char*)" ready=");printfHex32(baseline);printf((char*)"\n");}
    for (unsigned i=0;i<4;++i) {
        const unsigned mode=i==3?2:0,before=Record(runtime,peer).peerProgress;uint32_t id=0;
        {InterruptGuard admission;rootId=Create(runtime,module,mode,peerRecord.peerFile);id=rootId;NativeStatus admitted={};Need(runtime.Status(id,admitted)&&admitted.directory,"published root directory");rootDirectory=admitted.directory;}
        uint32_t began=tasks.Ticks();NativeStatus status={};
        do {Need(runtime.Status(id,status),"retained runtime status");if (!status.live) break;Wait(tasks,1);} while ((uint32_t)(tasks.Ticks()-began)<1500);
        DirectoryRecord record=Record(runtime,id);
        {InterruptGuard printGuard;printf((char*)"DIRECTORY STOP mode=");printfHex32(mode);printf((char*)" exit=");printfHex32(status.exitCode);printf((char*)" line=");printfHex32(record.failedLine);printf((char*)" phase=");printfHex32(record.phase);printf((char*)" checks=");printfHex32(record.checks);printf((char*)" tls_ctor=");printfHex32(record.tlsConstructed);printf((char*)" tls_dtor=");printfHex32(record.tlsDestroyed);printf((char*)"\n");}
        Need(!status.live&&status.observedCs==0x23&&status.observedCr3==status.directory&&status.systemCalls&&record.preinit==1&&!record.failedLine&&record.fileChecks&&record.heapBlocks,"real C runtime/heap/file execution");
        if (!mode) Need(status.exitCode==0&&status.faultVector==0&&record.phase==DirectoryDone&&record.joined==3&&record.workerDone==7&&record.tlsConstructed==4&&record.tlsDestroyed==4&&record.threadChecks,"real identity/detach/copy/TLS cleanup and slot reuse");
        else Need(status.faultVector==14&&status.faultError==6&&status.faultAddress==0xBFFFCFFCU&&record.phase==DirectoryRetained&&record.workerStage[0]==1&&record.workerStage[1]==1&&record.tlsConstructed==3&&record.tlsDestroyed==0,"whole-group fault with real TLS/heap retained");
        Need(record.retained==5&&store.OpenCount()==6,"five true leaked aliases and independent peer");NativeFileStatistics old=endpoint.Statistics();disk.failFlush=i==1;uint32_t reaped=0;
        {InterruptGuard reclaim;rootId=0;rootDirectory=0;reaped=runtime.Reap();}
        disk.failFlush=false;NativeFileStatistics now=endpoint.Statistics();
        {InterruptGuard printGuard;printf((char*)"DIRECTORY COUNTS reaped=");printfHex32(reaped);printf((char*)" open=");printfHex32(store.OpenCount());printf((char*)" free=");printfHex32(frames.getStatistics().freeFrames);printf((char*)" expected=");printfHex32(baseline);printf((char*)"\n");}
        Need(reaped==(mode?3U:1U)&&frames.getStatistics().freeFrames==baseline&&store.OpenCount()==1,"exact process/group physical and descriptor baseline");
        Need(now.reclaims==old.reclaims+1&&now.released==old.released+5&&now.reclaimFailures==old.reclaimFailures+(i==1?1U:0U)&&now.lastReclaimError==(i==1?-5:0),"real normal/fault/error owner cleanup");
        Wait(tasks,8);NativeStatus peerStatus={};Need(runtime.Status(peer,peerStatus)&&peerStatus.live&&Record(runtime,peer).peerProgress>before&&store.Seek(peer,peerRecord.peerFile,0,1)==0&&store.Size(peer,peerRecord.peerFile)==4,"peer file/cursor/task survive");
        {InterruptGuard printGuard;printf((char*)"DIRECTORY REAP released=");printfHex32(now.released-old.released);printf((char*)" error=");printfHex32((uint32_t)now.lastReclaimError);printf((char*)" free=");printfHex32(frames.getStatistics().freeFrames);printf((char*)" expected=");printfHex32(baseline);printf((char*)"\n");}
    }
    Need(runtime.RequestExit(peer,0)&&runtime.Reap()==1&&frames.getStatistics().freeFrames==initial&&!store.OpenCount(),"final exact peer/physical/descriptor baseline");NativeFileStatistics fs=endpoint.Statistics();NativeFpStatistics fp=runtime.FpStatistics();
    Need(calls==292&&successes==272&&errors==20&&peak==64&&closeErrors==4&&!heapOpenCalls&&fs.reclaims==5&&fs.released==21&&fs.reclaimFailures==1
        &&fp.initialized==16&&fp.invalidated==16&&fp.saves&&fp.restores&&!fp.invariantFailures&&irqs>10,"actual C API/capacity/heap/FP/error totals");Need(store.Unmount()==0,"no underlying opens");
    printf((char*)"DIRECTORY FINAL calls=");printfHex32(calls);printf((char*)" success=");printfHex32(successes);printf((char*)" errors=");printfHex32(errors);printf((char*)" close_errors=");printfHex32(closeErrors);printf((char*)" heap_opens=");printfHex32(heapOpenCalls);printf((char*)" released=");printfHex32(fs.released);printf((char*)" fp_init=");printfHex32(fp.initialized);printf((char*)" fp_invalid=");printfHex32(fp.invalidated);printf((char*)" fp_failures=");printfHex32(fp.invariantFailures);printf((char*)" free=");printfHex32(frames.getStatistics().freeFrames);printf((char*)" expected=");printfHex32(initial);printf((char*)"\n");Finish(true);
}
