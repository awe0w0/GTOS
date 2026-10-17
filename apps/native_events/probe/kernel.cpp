#include <process/native_runtime.h>
#include <process/native_events.h>
#include <hardwarecommunication/interrupts.h>
#include <syscalls.h>
#include <memory/criticalsection.h>
#include "record.h"
using namespace gtos;
using namespace gtos::process;
using namespace gtos::memory;
using namespace gtos::hardwarecommunication;
namespace gtos {
struct NativeEventFixture {static void ExhaustAfterOne(NativeEvents& e) {e.nextId=0x7fffffffU;}};
struct NativeClockFixture {static void Rollover(TaskManager& t) {t.ticks=0xfffffffeU;}};
}
extern "C" uint8_t kernel_start,kernel_end,kernel_readonly_start,kernel_readonly_end;
void printf(char* text) {while (*text) {asm volatile("outb %0,$0xe9"::"a"(*text));++text;}}
void printfHex32(uint32_t value) {for (int shift=28;shift>=0;shift-=4) {char c="0123456789ABCDEF"[(value>>shift)&15];asm volatile("outb %0,$0xe9"::"a"(c));}}
void printfHex(uint8_t value) {printfHex32(value);}
void operator delete(void*) {__builtin_trap();}
namespace {
    uint8_t image[2*1024*1024];uint32_t irqs,parks,immediate,rootDirectory;bool rollover;
    void Finish(bool pass) __attribute__((noreturn));
    void Finish(bool pass) {
        printf(pass?(char*)"NATIVE EVENTS PASS\n":(char*)"NATIVE EVENTS FAIL\n");
        asm volatile("outl %0,%1"::"a"(pass?0x10U:0x20U),"Nd"((uint16_t)0xf4));for (;;) asm volatile("cli; hlt");
    }
    void Need(bool pass,const char* why) {if (!pass) {printf((char*)"FAILED EVENTS ");printf((char*)why);printf((char*)"\n");Finish(false);}}
    uint32_t Flags() {uint32_t v;asm volatile("pushfl; popl %0":"=r"(v));return v;}
    uint32_t Cr3() {uint32_t v;asm volatile("mov %%cr3,%0":"=r"(v));return v;}
    void WaitIrq() {unsigned before=irqs;while (irqs==before) asm volatile("sti; hlt":::"memory");}
    void Field(const char* name,uint32_t value) {printf((char*)name);printfHex32(value);}
    uint32_t Get32(const uint8_t* p) {return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
    void Put32(uint8_t* p,uint32_t v) {for (unsigned i=0;i<4;++i) p[i]=v>>(8*i);}
    EventRecord Record(NativeRuntime& runtime,unsigned id) {EventRecord r={};Need(runtime.ReadMemory(id,EVENT_RECORD_VA,&r,sizeof(r)),"desktop fixed record read");return r;}
    unsigned Create(NativeRuntime& runtime,const MultibootModule& module,unsigned mode,unsigned shared,unsigned foreign) {
        Need(module.end>module.start&&module.end-module.start<=sizeof(image),"bounded ELF");unsigned size=module.end-module.start;
        for (unsigned i=0;i<size;++i) image[i]=((const uint8_t*)module.start)[i];
        Elf32LoadPlan plan;Need(ValidateElf32(image,size,plan),"ELF admission plan");bool found=false;
        for (unsigned i=0;i<plan.segmentCount;++i) {const Elf32LoadSegment& s=plan.segments[i];if (s.virtualAddress!=EVENT_RECORD_VA) continue;
            Need(!found&&(s.flags&Elf32Write)&&!(s.flags&Elf32Execute)&&s.fileSize>=sizeof(EventRecord)&&Get32(image+s.fileOffset)==1,"initialized fixed user record");
            Put32(image+s.fileOffset+4,mode);Put32(image+s.fileOffset+8,shared);Put32(image+s.fileOffset+12,foreign);found=true;
        }
        unsigned id=0;Need(found&&runtime.CreateElf(image,size,id)&&id,"canonical ELF launch");return id;
    }
    void Limits() {
        NativeEvents e;uint32_t ids[64];
        Need(e.CreateForOwner(0,0)==GTOS_EVENT_ERR_INVALID&&e.CreateForOwner(1,4)==GTOS_EVENT_ERR_INVALID,"invalid kernel create");
        for (unsigned i=0;i<32;++i) {int id=e.CreateForOwner(1,0);Need(id>0,"all real object slots");ids[i]=(unsigned)id;}
        Need(e.CreateForOwner(1,0)==GTOS_EVENT_ERR_LIMIT&&e.Statistics().objects==32&&e.Statistics().handles==32,"object limit atomic");
        for (unsigned i=32;i<64;++i) {int id=e.DuplicateForOwner(1,ids[0],1,0);Need(id>0,"all real alias slots");ids[i]=(unsigned)id;}
        Need(e.DuplicateForOwner(1,ids[0],1,0)==GTOS_EVENT_ERR_LIMIT&&e.Statistics().handles==64,"handle limit atomic");
        Need(e.Set(2,ids[0])==GTOS_EVENT_ERR_BAD_HANDLE&&e.Close(2,ids[0])==GTOS_EVENT_ERR_BAD_HANDLE,"kernel foreign-owner rejection");
        e.ReclaimOwner(1);Need(!e.Statistics().handles&&!e.Statistics().objects&&e.Statistics().released==64,"capacity exact reclaim");
        int fresh=e.CreateForOwner(1,2);Need(fresh>64&&e.Select(1,&ids[0],1)==GTOS_EVENT_ERR_BAD_HANDLE,"stale handle never reused");e.ReclaimOwner(1);
        NativeEventFixture::ExhaustAfterOne(e);Need(e.CreateForOwner(1,0)==0x7fffffff,"last positive handle");
        Need(e.CreateForOwner(1,0)==GTOS_EVENT_ERR_OVERFLOW&&e.DuplicateForOwner(1,0x7fffffff,1,0)==GTOS_EVENT_ERR_OVERFLOW&&e.Statistics().handles==1,"ID exhaustion atomic");
        e.ReclaimOwner(1);Need(!e.Statistics().handles&&!e.Statistics().objects,"overflow owner cleanup");
        printf((char*)"EVENT LIMITS objects=00000020 handles=00000040 overflow=00000001\n");
    }
    class Timer:public InterruptHandler {
    public:
        Timer(InterruptsManager& i):InterruptHandler(&i,0x20) {}
        uint32_t HandlerInterrupt(uint32_t esp) {Need(!(Flags()&0x200),"timer IF0");++irqs;return esp;}
    };
    class Tap:public InterruptHandler {
        TaskManager& tasks;PhysicalMemoryManager& frames;NativeEvents& events;SyscallHandler& original;bool reslept;
    public:
        Tap(InterruptsManager& i,TaskManager& t,PhysicalMemoryManager& f,NativeEvents& e,SyscallHandler& s):InterruptHandler(&i,0x80),tasks(t),frames(f),events(e),original(s),reslept(false) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            CPUState* cpu=(CPUState*)esp;Need((cpu->cs&3)==3&&!(Flags()&0x200),"CPL3 trap and IF0");
            if (!rollover&&rootDirectory&&Cr3()==rootDirectory&&cpu->eax==GTOS_SYS_TICKS
                &&((const volatile EventRecord*)EVENT_RECORD_VA)->phase==40) {NativeClockFixture::Rollover(tasks);rollover=true;}
            if (cpu->eax!=GTOS_SYS_EVENT_WAIT) return original.HandlerInterrupt(esp);
            const CPUState saved=*cpu;Task* task=tasks.CurrentTask();Need(task&&task->UserMode(),"real native wait task");
            unsigned externalHandle=0;
            if (!reslept&&rootDirectory&&Cr3()==rootDirectory&&((const volatile EventRecord*)EVENT_RECORD_VA)->phase==42) {
                Need(cpu->ecx==sizeof(GtosEventWaitRequest)&&cpu->ebx>=NativeRuntime::UserStackBottom&&cpu->ebx<=NativeRuntime::UserStackTop-sizeof(GtosEventWaitRequest),"fixed root request in admitted stack");
                const volatile GtosEventWaitRequest& request=*(const volatile GtosEventWaitRequest*)cpu->ebx;
                Need(request.version==1&&request.count==1&&request.timeout_ticks==3&&request.handles_va>=NativeRuntime::UserStackBottom&&request.handles_va<=NativeRuntime::UserStackTop-4,"exact known test wait array");
                externalHandle=*(const volatile unsigned*)request.handles_va;Need(externalHandle,"known event alias");
            }
            unsigned free=frames.getStatistics().freeFrames,before=irqs,sleeps=task->Statistics().sleeps;
            unsigned result=original.HandlerInterrupt(esp);
            Need(cpu->ebx==saved.ebx&&cpu->ecx==saved.ecx&&cpu->edx==saved.edx&&cpu->esi==saved.esi&&cpu->edi==saved.edi&&cpu->ebp==saved.ebp
                &&cpu->ds==saved.ds&&cpu->es==saved.es&&cpu->fs==saved.fs&&cpu->gs==saved.gs,"wait preserves non-result registers");
            Need(frames.getStatistics().freeFrames==free&&irqs==before,"wait no pages/no fake IRQ");
            if (task->State()==TaskSleeping) {Need(task->Statistics().sleeps==sleeps+1&&tasks.CurrentTask()!=task&&result!=esp,"true kernel parking");++parks;}
            else {Need(task->Statistics().sleeps==sleeps&&result==esp,"immediate wait does not sleep");++immediate;}
            if (externalHandle) {
                Need(task->State()==TaskSleeping&&tasks.WakeTask(task)&&tasks.SleepTask(task,5),"external kernel wake and new sleep");
                Need(events.Set(1,externalHandle)==0&&task->State()==TaskSleeping&&(int32_t)cpu->eax==GTOS_EVENT_ERR_TIMEOUT,"old event metadata cannot consume signal or wake unrelated sleep");
                reslept=true;printf((char*)"EVENT RESLEEP isolated=00000001\n");
            }
            return result;
        }
    };
}
extern "C" __attribute__((noreturn)) void __cxa_pure_virtual() {Finish(false);}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"NATIVE EVENTS BOOT\n");Limits();
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;Need(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical allocator");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    Need(paging.prepareIdentity(frames,config),"identity map");NativeRuntime runtime;NativeEvents events;
    Need(runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors()&&runtime.AttachEvents(events)&&!runtime.AttachEvents(events),"sealed guarded stacks and single notifier");
    Need(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2,true)&&!runtime.AttachEvents(events),"real SSE2/thread runtime");Timer timer(interrupts);Tap tap(interrupts,tasks,frames,events,syscalls);
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;Need((boot->flags&8)&&boot->moduleCount==1,"one test user ELF");const MultibootModule& module=*(const MultibootModule*)boot->modules;
    unsigned initial=frames.getStatistics().freeFrames;
    int grant=events.CreateForOwner(1,3),peerGrant=events.DuplicateForOwner(1,(unsigned)grant,2,GTOS_EVENT_RIGHT_WAIT|GTOS_EVENT_RIGHT_SET);
    Need(grant>0&&peerGrant>0,"trusted scoped admission grants");unsigned root=Create(runtime,module,0,(unsigned)grant,(unsigned)peerGrant),peer=Create(runtime,module,1,(unsigned)peerGrant,(unsigned)grant);
    Need(root==1&&peer==2,"explicit admission owner IDs");NativeStatus status={};Need(runtime.Status(root,status),"root directory status");rootDirectory=status.directory;
    unsigned baseline=frames.getStatistics().freeFrames;interrupts.Activate();unsigned began=irqs;
    do {Need(runtime.Status(root,status),"root live status");if (!status.live) break;Need(irqs-began<1000,"bounded normal runtime");WaitIrq();} while (true);
    EventRecord result=Record(runtime,root);Field("EVENT ROOT exit=",status.exitCode);Field(" line=",result.failedLine);Field(" checks=",result.checks);Field(" phase=",result.phase);printf((char*)"\n");
    Need(!result.failedLine&&result.phase==99&&status.exitCode==0&&!status.faultVector&&status.observedCs==0x23&&status.observedCr3==status.directory&&rollover,"actual native wire/wait/thread semantics");
    Need(events.Statistics().handles==2&&events.Statistics().objects==1,"only actual admission grants retained");
    Need(runtime.Reap()==1&&events.Statistics().handles==1&&events.Statistics().objects==1,"normal owner release preserves peer alias");rootDirectory=0;
    unsigned peerBaseline=frames.getStatistics().freeFrames;Need(peerBaseline>baseline&&Record(runtime,peer).phase==10,"peer steady baseline");
    unsigned progress=Record(runtime,peer).progress;for (unsigned i=0;i<8;++i) WaitIrq();Need(Record(runtime,peer).progress>progress,"peer keeps observing same manual event after original owner reap");
    root=Create(runtime,module,2,0,0);began=irqs;
    do {Need(runtime.Status(root,status),"fault-group status");if (!status.live) break;Need(irqs-began<1000,"bounded fault runtime");WaitIrq();} while (true);
    result=Record(runtime,root);Need(!result.failedLine&&result.phase==30&&result.workerStage[0]==1&&result.workerStage[1]==1&&status.faultVector==14&&status.faultError==6&&status.faultAddress==0xBFFFCFFCU,"fault stops real parked workers");
    Need(events.Statistics().handles==4&&events.Statistics().objects==4,"three real fault-owner events retained before deferred reap");
    Need(runtime.Reap()==3&&frames.getStatistics().freeFrames==peerBaseline&&events.Statistics().handles==1&&events.Statistics().objects==1,"fault group exact pages and events recovery");
    progress=Record(runtime,peer).progress;for (unsigned i=0;i<8;++i) WaitIrq();Need(Record(runtime,peer).progress>progress,"independent peer after fault reap");
    Need(runtime.RequestExit(peer,0)&&runtime.Reap()==1&&frames.getStatistics().freeFrames==initial&&!events.Statistics().handles&&!events.Statistics().objects,"final physical and alias/object baseline");
    NativeFpStatistics fp=runtime.FpStatistics();Need(fp.initialized==9&&fp.invalidated==9&&fp.saves&&fp.restores&&!fp.invariantFailures&&parks>=8&&immediate>=20,"real parking/FP lifecycle totals");
    Field("EVENT FINAL parks=",parks);Field(" immediate=",immediate);Field(" fp_init=",fp.initialized);Field(" fp_invalid=",fp.invalidated);
    Field(" free=",frames.getStatistics().freeFrames);Field(" expected=",initial);Field(" handles=",events.Statistics().handles);Field(" objects=",events.Statistics().objects);printf((char*)"\n");Finish(true);
}
