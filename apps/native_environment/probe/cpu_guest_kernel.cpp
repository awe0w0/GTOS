#include <process/native_runtime.h>
#include <hardwarecommunication/interrupts.h>
#include <syscalls.h>
#include <memory/criticalsection.h>
#include "cpu_guest_record.h"
using namespace gtos;
using namespace gtos::memory;
using namespace gtos::process;
using namespace gtos::hardwarecommunication;
extern "C" uint8_t kernel_start,kernel_end,kernel_readonly_start,kernel_readonly_end;
void printf(char* text) { while (*text) { asm volatile("outb %0,$0xe9" : : "a"(*text)); ++text; } }
void printfHex32(uint32_t value) {
    for (int shift=28;shift>=0;shift-=4) {
        char c="0123456789ABCDEF"[(value>>shift)&15]; asm volatile("outb %0,$0xe9" : : "a"(c));
    }
}
void printfHex(uint8_t value) { printfHex32(value); }
void operator delete(void*) {}
namespace {
    struct Sample { uint32_t thread, process, ticks, lastThread, lastProcess, boots; } samples[32];
    uint32_t sampleCount, calls, errors, peak, observedIrqs, environmentCalls, environmentErrors;
    char environmentData[GTOS_ENVIRONMENT_DATA_BYTES+1];
    uint8_t cpuElf[65536];
    void CpuFinish(bool pass) __attribute__((noreturn));
    void CpuFinish(bool pass) {
        printf(pass ? (char*)"CPU CLOCK GUEST PASS\n" : (char*)"CPU CLOCK GUEST FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass?0x10U:0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void Need(bool value,const char* why) {
        if (!value) { printf((char*)"FAILED CPU "); printf((char*)why); printf((char*)"\n"); CpuFinish(false); }
    }
    uint32_t Get32(const uint8_t* p) {
        return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
    }
    void Put32(uint8_t* p,uint32_t value) { for (uint32_t i=0;i<4;++i) p[i]=value>>(8*i); }
    void WaitTicks(TaskManager& tasks,uint32_t ticks) {
        uint32_t begin=tasks.Ticks();
        while ((uint32_t)(tasks.Ticks()-begin)<ticks) asm volatile("sti; hlt" : : : "memory");
    }
    NativeStatus WaitStopped(NativeRuntime& runtime,TaskManager& tasks,uint32_t id) {
        uint32_t begin=tasks.Ticks(); NativeStatus status={};
        do {
            Need(runtime.Status(id,status),"status exists");
            if (!status.live) return status;
            WaitTicks(tasks,1);
        } while ((uint32_t)(tasks.Ticks()-begin)<500);
        Need(false,"bounded guest completion"); return status;
    }
    Sample& Count(uint32_t thread,uint32_t process) {
        for (uint32_t i=0;i<sampleCount;++i) if (samples[i].thread==thread) {
            Need(samples[i].process==process,"thread cannot change process"); return samples[i];
        }
        Need(thread && process && sampleCount<32,"bounded independent identity table");
        Sample& sample=samples[sampleCount++]; sample.thread=thread; sample.process=process;
        sample.ticks=0; sample.lastThread=sample.lastProcess=~0U; return sample;
    }
    uint32_t ProcessTicks(uint32_t id) {
        uint32_t n=0; for (uint32_t i=0;i<sampleCount;++i) if (samples[i].process==id) n+=samples[i].ticks;
        return n;
    }
    uint32_t Flags() { uint32_t f; asm volatile("pushfl; popl %0":"=r"(f)); return f; }
}
// Clang emits a strong pure-virtual vtable reference at O0. Any actual call
// is a test failure; this supplies no successful service or fallback behavior.
extern "C" __attribute__((noreturn)) void __cxa_pure_virtual() {
    printf((char*)"FAILED CPU pure virtual dispatch\n"); CpuFinish(false);
}
namespace gtos {
    // Existing acceptance friend only; no production user seed/inspection API.
    struct NativeClockFixture {
        static bool Identity(NativeRuntime& runtime,uint32_t& thread,uint32_t& process) {
            NativeRuntime::Slot* slot=runtime.Current();
            if (!slot || !slot->status.live || slot->task.State()!=TaskRunning) return false;
            thread=slot->status.id; process=slot->process->status.id; return true;
        }
        static bool Copy(NativeRuntime& runtime,void* out,uint32_t address,uint32_t bytes) {
            NativeRuntime::Slot* slot=runtime.Current(); return slot && slot->Space().CopyFromUser(out,address,bytes);
        }
    };
}
namespace {
    class CpuTimerTap : public InterruptHandler {
        NativeRuntime& runtime;
    public:
        CpuTimerTap(InterruptsManager& interrupts,NativeRuntime& r):InterruptHandler(&interrupts,0x20),runtime(r) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            Need(!(Flags()&0x200),"IRQ IF0"); ++observedIrqs;
            uint32_t thread=0,process=0;
            if (NativeClockFixture::Identity(runtime,thread,process)) ++Count(thread,process).ticks;
            return esp;
        }
    };
    class CpuSyscallTap : public InterruptHandler {
        NativeRuntime& runtime; TaskManager& tasks; PhysicalMemoryManager& frames; SyscallHandler& original;
    public:
        CpuSyscallTap(InterruptsManager& interrupts,NativeRuntime& r,TaskManager& t,PhysicalMemoryManager& f,SyscallHandler& h)
            :InterruptHandler(&interrupts,0x80),runtime(r),tasks(t),frames(f),original(h) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            CPUState* cpu=(CPUState*)esp;
            NativeStatistics stats=runtime.Statistics();
            uint32_t live=stats.created-stats.reaped; if (live>peak) peak=live;
            Need(live<=4,"native capacity unchanged");
            if (cpu->eax==GTOS_SYS_ENVIRONMENT_READ) {
                const uint32_t before=frames.getStatistics().freeFrames;
                CpuGuestRecord record={};Need(NativeClockFixture::Copy(runtime,&record,CPU_RECORD_VA,sizeof(record)),"environment calling record");
                if (!record.phase) {
                    uint32_t thread=0,process=0;Need(NativeClockFixture::Identity(runtime,thread,process),"environment bootstrap identity");
                    ++Count(thread,process).boots;
                }
                uint32_t returned=original.HandlerInterrupt(esp); ++environmentCalls;
                Need(returned==esp && frames.getStatistics().freeFrames==before,"environment query allocates nothing and does not schedule");
                if ((int32_t)cpu->eax<0) ++environmentErrors;
                return returned;
            }
            if (cpu->eax!=GTOS_SYS_CPU_CLOCK_READ) return original.HandlerInterrupt(esp);
            Need(!(Flags()&0x200) && (cpu->cs&3)==3,"actual CPU syscall CPL3/IF0");
            uint32_t thread=0,process=0;
            Need(NativeClockFixture::Identity(runtime,thread,process),"CPU caller identity");
            const CPUState before=*cpu; Task* current=tasks.CurrentTask();
            const uint32_t free=frames.getStatistics().freeFrames, irqs=observedIrqs;
            GtosCpuClockReadRequest request={};
            bool copied=cpu->ecx==sizeof(request) && NativeClockFixture::Copy(runtime,&request,cpu->ebx,sizeof(request));
            uint32_t returned=original.HandlerInterrupt(esp);
            Need(returned==esp && tasks.CurrentTask()==current && observedIrqs==irqs
                && frames.getStatistics().freeFrames==free,"query has no scheduling/time/allocation side effects");
            Need(cpu->ebx==before.ebx && cpu->ecx==before.ecx && cpu->edx==before.edx
                && cpu->esi==before.esi && cpu->edi==before.edi && cpu->ebp==before.ebp
                && cpu->ds==before.ds && cpu->es==before.es && cpu->fs==before.fs && cpu->gs==before.gs,
                "non-result registers preserved"); ++calls;
            if (cpu->eax) {
                Need((int32_t)cpu->eax==-14 || (int32_t)cpu->eax==-22 || (int32_t)cpu->eax==-38,
                    "only specified negative errors in ordinary scenario"); ++errors; return returned;
            }
            GtosCpuClockReadResult result={};
            Need(copied && NativeClockFixture::Copy(runtime,&result,request.result_va,sizeof(result)),"actual result copy");
            Sample& own=Count(thread,process);
            uint32_t expected=request.scope==1?own.ticks:ProcessTicks(process);
            Need(result.version==1 && result.scope==request.scope && result.unit==1 && result.source==1
                && result.capabilities==7 && result.resolution_us==10000 && result.charged_ticks==expected
                && result.pit_input_hz==1193182 && result.pit_divisor==11931,"independent IRQ ownership and domain");
            uint32_t& previous=request.scope==1?own.lastThread:own.lastProcess;
            if (previous!=expected) {
                previous=expected;
                printf((char*)"CPU SAMPLE tid="); printfHex32(thread); printf((char*)" pid="); printfHex32(process);
                printf((char*)" scope="); printfHex32(request.scope); printf((char*)" ticks="); printfHex32(expected);
                printf((char*)" us_hi="); printfHex32((uint32_t)(result.microseconds>>32));
                printf((char*)" us_lo="); printfHex32((uint32_t)result.microseconds); printf((char*)"\n");
            }
            return returned;
        }
    };
    bool Equal(const GtosEnvironmentReadResult& a,const GtosEnvironmentReadResult& b) {
        const unsigned char* x=(const unsigned char*)&a; const unsigned char* y=(const unsigned char*)&b;
        for (unsigned i=0;i<sizeof(a);++i) if (x[i]!=y[i]) return false;
        return true;
    }
    void EnvironmentBuilderCases() {
        NativeEnvironment environment;
        const char good[]="A=1\0AB=2\0EMPTY=\0VALUE=x=y\0";
        Need(environment.Assign(good,sizeof(good)-1),"valid environment builder");
        const GtosEnvironmentReadResult saved=environment.Snapshot();
        const char duplicate[]="A=1\0A=2\0", noName[]="=v", noEqual[]="NAME", noTerminator[]={'A','=','1'};
        Need(!environment.Assign(duplicate,sizeof(duplicate)-1) && Equal(saved,environment.Snapshot()),"duplicate transactional rejection");
        Need(!environment.Assign(noName,sizeof(noName)) && Equal(saved,environment.Snapshot()),"empty name transactional rejection");
        Need(!environment.Assign(noEqual,sizeof(noEqual)) && Equal(saved,environment.Snapshot()),"missing equal transactional rejection");
        Need(!environment.Assign(noTerminator,sizeof(noTerminator)) && Equal(saved,environment.Snapshot()),"unterminated transactional rejection");
        Need(!environment.Assign(0,1) && !environment.Assign(good,3825) && Equal(saved,environment.Snapshot()),"null and overcapacity transactional rejection");
        Need(environment.Assign(environment.Snapshot().data,environment.Snapshot().data_bytes)
            && Equal(saved,environment.Snapshot()),"self source alias");
        unsigned at=0;
        for (unsigned i=0;i<65;++i) {
            environmentData[at++]='Q'; environmentData[at++]='0'+i/10; environmentData[at++]='0'+i%10;
            environmentData[at++]='='; environmentData[at++]=0;
        }
        Need(!environment.Assign(environmentData,at) && Equal(saved,environment.Snapshot()),"65 entries transactional rejection");
        Need(environment.Assign(environmentData,at-5) && environment.Snapshot().count==64,"64 entries accepted");
        environmentData[0]='A'; environmentData[1]='=';
        for (unsigned i=2;i<3823;++i) environmentData[i]='Z'; environmentData[3823]=0;
        Need(environment.Assign(environmentData,3824) && environment.Snapshot().data_bytes==3824,"full byte capacity");
        Need(!environment.Assign(environmentData,3823) && environment.Snapshot().data_bytes==3824,"truncated full capacity rejected");
        Need(environment.Assign(0,0) && !environment.Snapshot().count && !environment.Snapshot().data_bytes,"empty environment accepted");
    }
    uint32_t CpuCreate(NativeRuntime& runtime,const MultibootModule& module,uint32_t mode,uint32_t environmentCase) {
        Need(module.end>module.start && module.end-module.start<=sizeof(cpuElf),"bounded module");
        uint32_t size=module.end-module.start;
        for (uint32_t i=0;i<size;++i) cpuElf[i]=((const uint8_t*)module.start)[i];
        Elf32LoadPlan plan; Need(ValidateElf32(cpuElf,size,plan),"real ELF32 validation");
        bool found=false;
        for (uint32_t i=0;i<plan.segmentCount;++i) {
            const Elf32LoadSegment& segment=plan.segments[i];
            if (segment.virtualAddress!=CPU_RECORD_VA) continue;
            Need(!found && (segment.flags&Elf32Write) && !(segment.flags&Elf32Execute)
                && segment.fileSize>=sizeof(CpuGuestRecord) && Get32(cpuElf+segment.fileOffset)==1
                && Get32(cpuElf+segment.fileOffset+4)==0,"single writable initialized record");
            Put32(cpuElf+segment.fileOffset+4,mode);
            Put32(cpuElf+segment.fileOffset+__builtin_offsetof(CpuGuestRecord,environmentCase),environmentCase);found=true;
        }
        Need(found,"only mode and environment case annotated in kernel load-copy");
        NativeEnvironment environment;
        if (environmentCase==0) {
            const char prefix[]="FORCE_COLOR=1\0NO_COLOR=\0A=x=y\0EMPTY=\0";
            unsigned at=0;
            for (;at<sizeof(prefix)-1;++at) environmentData[at]=prefix[at];
            for (unsigned i=0;i<60;++i) {
                environmentData[at++]='N'; environmentData[at++]='0'+i/10; environmentData[at++]='0'+i%10; environmentData[at++]='=';
                if (i==59) while (at<3823) environmentData[at++]='Z';
                environmentData[at++]=0;
            }
            Need(at==3824 && environment.Assign(environmentData,at),"full startup environment");
        } else if (environmentCase==1) {
            const char peer[]="FORCE_COLOR=\0NO_COLOR=1\0PEER=alone\0";
            Need(environment.Assign(peer,sizeof(peer)-1),"peer environment");
        } else if (environmentCase==2) {
            const char data[]="FORCE_COLOR=0\0NO_COLOR=1\0";Need(environment.Assign(data,sizeof(data)-1),"force zero nonempty");
        } else if (environmentCase==3) {
            const char data[]="FORCE_COLOR=1\0NO_COLOR=1\0";Need(environment.Assign(data,sizeof(data)-1),"force wins no color");
        } else if (environmentCase==4) {
            const char data[]="FORCE_COLOR=\0NO_COLOR=\0";Need(environment.Assign(data,sizeof(data)-1),"empty overrides");
        } else if (environmentCase==5) {
            const char data[]="NO_COLOR=1\0";Need(environment.Assign(data,sizeof(data)-1),"no color only");
        } else if (environmentCase==6) {
            const char data[]="FORCE_COLOR=1\0";Need(environment.Assign(data,sizeof(data)-1),"force only");
        } else Need(environmentCase==7,"empty case");
        uint32_t id=0;
        Need((mode==2 ? runtime.CreateElf(cpuElf,size,id) : runtime.CreateElf(cpuElf,size,id,environment)) && id,"CPU ELF admission with own environment");
        environment.Clear();
        for (unsigned i=0;i<sizeof(environmentData);++i) environmentData[i]='!';
        return id;
    }
    CpuGuestRecord Record(NativeRuntime& runtime,uint32_t id) {
        CpuGuestRecord record={}; Need(runtime.ReadMemory(id,CPU_RECORD_VA,&record,sizeof(record)),"read retained user record");
        return record;
    }
}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"CPU CLOCK GUEST BOOT\n");
    EnvironmentBuilderCases();
    GlobalDescriptorTable gdt; TaskManager tasks;
    InterruptsManager interrupts(0x20,&gdt,&tasks); SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;
    Need(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical frames");
    KernelPaging paging;
    PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,
        (uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    Need(paging.prepareIdentity(frames,config),"identity paging"); NativeRuntime runtime;
    Need(runtime.PrepareStacks(paging,frames) && paging.enable() && paging.sealForSharedProcessors(),"guard stacks and sealed paging");
    Need(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2,true),"real SSE2 runtime with four-context thread policy");
    CpuTimerTap timer(interrupts,runtime); CpuSyscallTap tap(interrupts,runtime,tasks,frames,syscalls);
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;
    Need((boot->flags&8) && boot->moduleCount==1,"one immutable raw CPU test ELF");
    const MultibootModule& module=*(const MultibootModule*)boot->modules;
    const uint32_t initial=frames.getStatistics().freeFrames;
    const uint32_t peer=CpuCreate(runtime,module,1,1);
    const uint32_t peerBaseline=frames.getStatistics().freeFrames;
    interrupts.Activate(); WaitTicks(tasks,8);
    Need(Record(runtime,peer).phase==1,"peer runs at CPL3");
    const uint32_t cases[]={0,2,3,4,5,6,7,7};
    for (uint32_t modeIndex=0;modeIndex<8;++modeIndex) {
        const uint32_t mode=modeIndex==7?2:0;
        uint32_t before=Record(runtime,peer).checks;
        uint32_t id=CpuCreate(runtime,module,mode,cases[modeIndex]);
        NativeStatus status=WaitStopped(runtime,tasks,id); CpuGuestRecord record=Record(runtime,id);
        Need(status.observedCs==0x23 && status.observedCr3==status.directory && status.systemCalls,
            "native user execution and own CR3 observed");
        { InterruptGuard guard;
            printf((char*)"CPU STOP mode="); printfHex32(mode); printf((char*)" exit="); printfHex32(status.exitCode);
            printf((char*)" fault="); printfHex32(status.faultVector); printf((char*)" phase="); printfHex32(record.phase);
            printf((char*)" joined="); printfHex32(record.joined); printf((char*)"\n");
        }
        Need(record.environmentCase==cases[modeIndex] && record.constructorCalls==1,"actual getenv constructor case");
        { InterruptGuard guard;
            printf((char*)"ENV SDK case=");printfHex32(record.environmentCase);printf((char*)" ctor=");printfHex32(record.constructorCalls);printf((char*)"\n");
        }
        if (mode==0) Need(!status.exitCode && !status.faultVector && record.phase==2 && record.joined==3
            && record.workerDone==7 && record.checks>30,"actual two workers, joins and reused slot");
        else Need(status.faultVector==14 && status.faultError==6 && status.faultAddress==0xBFFFCFFCU
            && status.exitCode==(0x80000000U|14) && record.workerStarted==3,"whole-group user fault");
        Need(runtime.Reap()==(mode==0?1U:3U) && frames.getStatistics().freeFrames==peerBaseline,
            "exact group reap with peer frame baseline");
        WaitTicks(tasks,8); NativeStatus peerStatus={};
        Need(runtime.Status(peer,peerStatus) && peerStatus.live && Record(runtime,peer).checks>before,
            "unrelated peer survives and progresses");
        { InterruptGuard guard;
            printf((char*)"CPU REAP free="); printfHex32(frames.getStatistics().freeFrames);
            printf((char*)" expected="); printfHex32(peerBaseline); printf((char*)"\n");
        }
    }
    Need(runtime.RequestExit(peer,0) && runtime.Reap()==1 && frames.getStatistics().freeFrames==initial,
        "final exact physical frame baseline");
    Need(peak==4 && errors>=112 && calls>errors && observedIrqs>10 && sampleCount==32,"actual capacity, rejection and IRQ coverage");
    uint32_t bootstraps=0;
    for (uint32_t i=0;i<sampleCount;++i) {
        Need(samples[i].boots==(samples[i].thread==samples[i].process?1U:0U),"only root reads startup once before constructors");
        bootstraps+=samples[i].boots;
    }
    Need(bootstraps==9,"all nine process bootstraps");
    NativeFpStatistics fp=runtime.FpStatistics();
    Need(fp.initialized==32 && fp.invalidated==32 && fp.saves && fp.restores && !fp.invariantFailures,"real SSE2 state lifecycle");
    printf((char*)"CPU SSE2 initialized=");printfHex32(fp.initialized);printf((char*)" invalidated=");printfHex32(fp.invalidated);
    printf((char*)" saves=");printfHex32(fp.saves);printf((char*)" restores=");printfHex32(fp.restores);printf((char*)" failures=");printfHex32(fp.invariantFailures);printf((char*)"\n");
    printf((char*)"CPU FINAL peak="); printfHex32(peak); printf((char*)" calls="); printfHex32(calls);
    printf((char*)" errors="); printfHex32(errors); printf((char*)" identities="); printfHex32(sampleCount);
    printf((char*)" free="); printfHex32(frames.getStatistics().freeFrames); printf((char*)" expected="); printfHex32(initial);
    printf((char*)"\n");
    Need(environmentCalls>200 && environmentErrors>=126,"environment query/rejection/lifecycle coverage");
    printf((char*)"ENV FINAL calls="); printfHex32(environmentCalls); printf((char*)" errors="); printfHex32(environmentErrors);
    printf((char*)" bootstraps=");printfHex32(bootstraps);
    printf((char*)"\nENVIRONMENT SNAPSHOT GUEST PASS\nGETENV SSE2 GUEST PASS\n"); CpuFinish(true);
}
