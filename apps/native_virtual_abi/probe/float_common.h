#ifndef GTOS_NATIVE_FLOAT_KERNEL_COMMON_H
#define GTOS_NATIVE_FLOAT_KERNEL_COMMON_H
#include "fixture-helpers.h"
#include <memory/criticalsection.h>
#include <process/clock_abi.h>
#include "record.h"
namespace {
    uint32_t fileChecks,ids[16],idCount,kernelDirectory,maxTrapStack,diagnosticCalls[16],panicCalls[16],injectedResults[16];
    PhysicalMemoryManager* fileFrames;
    void FileFinish(bool pass) __attribute__((noreturn));
    void FileFinish(bool pass) {
        printf(pass?(char*)"NATIVE CXA VIRTUAL GUEST PASS\n":(char*)"NATIVE CXA VIRTUAL GUEST FAIL\n");
        asm volatile("outl %0,%1"::"a"(pass?0x10U:0x20U),"Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void FileNeed(bool pass,const char* why) {
        fileChecks++;
        if (!pass) { asm volatile("cli":::"memory");printf((char*)"FAILED CXA VIRTUAL ");printf((char*)why);printf((char*)"\n");FileFinish(false); }
    }
    void Value(const char* name,uint32_t x) { printf((char*)name);printfHex32(x); }
    FileRecord Record(NativeRuntime& r,uint32_t id) {
        FileRecord x={};FileNeed(r.ReadMemory(id,FILE_RECORD_VA,&x,sizeof(x)),"actual user float record");return x;
    }
    uint32_t Directory() { uint32_t value;asm volatile("mov %%cr3,%0":"=r"(value));return value; }
    void UserReadable(uint32_t cr3,uint32_t address,uint32_t bytes) {
        FileNeed(bytes&&bytes<=256&&address<=0xffffffffU-bytes,"bounded observed bytes");
        FileNeed((address>=0x40000000&&address<=0x40040000-bytes)
            ||(address>=FILE_RECORD_VA&&address<=FILE_RECORD_VA+4096-bytes)
            ||(address>=NativeRuntime::UserStackBottom&&address<=NativeRuntime::UserStackTop-bytes),"fixed admitted code/data/stack span");
        for (uint32_t page=address&~4095U;page<=((address+bytes-1)&~4095U);page+=4096) {
            const uint32_t pde=((const uint32_t*)cr3)[page>>22];
            FileNeed((pde&5)==5&&!(pde&0x80),"observed user PDE");
            const uint32_t pte=((const uint32_t*)(pde&~4095U))[(page>>12)&1023];
            FileNeed((pte&5)==5&&fileFrames->isAllocated(pte&~4095U),"observed owned user PTE");
        }
    }
    class FloatTap:public InterruptHandler {
        NativeRuntime& runtime;TaskManager& tasks;SyscallHandler& original;
    public:
        FloatTap(InterruptsManager& i,NativeRuntime& r,TaskManager& t,SyscallHandler& o):InterruptHandler(&i,0x80),runtime(r),tasks(t),original(o) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            CPUState* cpu=(CPUState*)esp;uint32_t sp;asm volatile("mov %%esp,%0":"=r"(sp));
            FileNeed(cpu->cs==0x23&&sp>=NativeRuntime::KernelStackArena&&sp<NativeRuntime::KernelStackArena+0x15000,"actual CPL3 syscall on bounded trap stack");
            const uint32_t index=(sp-NativeRuntime::KernelStackArena)/(5*4096);
            const uint32_t used=NativeRuntime::KernelStackArena+(index+1)*5*4096-sp;
            if (used>maxTrapStack) maxTrapStack=used;
            FileNeed(used<4*4096-512,"observed trap entry stack margin");
            FileNeed(cpu->eax<0x4720||cpu->eax>0x472f,"ABI fixture has no file operations");
            if (cpu->eax!=GTOS_SYS_WRITE) return original.HandlerInterrupt(esp);
            const uint32_t cr3=Directory();Task* current=tasks.CurrentTask();uint32_t id=0;NativeStatus initial={};
            FileNeed(current&&current->UserMode()&&cr3!=kernelDirectory,"real current private user task");
            for (uint32_t n=0;n<idCount;n++) {
                NativeStatus status={};if (runtime.Status(ids[n],status)&&status.live&&status.directory==cr3) { FileNeed(!id,"unique current owner");id=ids[n];initial=status; }
            }
            FileNeed(id&&id<16,"admitted current owner");UserReadable(cr3,FILE_RECORD_VA,sizeof(FileRecord));
            const FileRecord record=*(const FileRecord*)FILE_RECORD_VA;
            FileNeed(record.stage==1&&!record.error&&record.mode<8&&record.command==record.mode/2,"bounded configured ABI write");
            const bool diagnostic=diagnosticCalls[id]==0;
            const char* expected=diagnostic?((record.mode&1)?"Deleted virtual function called!":"Pure virtual function called!"):"GTOS NATIVE HEAP FAIL V1\n";
            const uint32_t bytes=diagnostic?record.mask:25U;
            FileNeed(cpu->ecx==bytes&&(diagnostic?!panicCalls[id]:diagnosticCalls[id]==1&&!panicCalls[id]),"single real diagnostic then panic write");
            UserReadable(cr3,cpu->ebx,bytes);
            for (uint32_t i=0;i<bytes;i++) FileNeed(((const char*)cpu->ebx)[i]==expected[i],"exact actual ABI diagnostic bytes");
            const CPUState before=*cpu;const uint32_t free=fileFrames->getStatistics().freeFrames,ticks=tasks.Ticks(),count=tasks.TaskCount();
            const uint32_t returned=original.HandlerInterrupt(esp);NativeStatus observed={};
            FileNeed(returned==esp&&cpu->eax==bytes&&tasks.CurrentTask()==current&&Directory()==cr3&&tasks.Ticks()==ticks
                &&tasks.TaskCount()==(int)count&&fileFrames->getStatistics().freeFrames==free,"real write preserves ownership");
            FileNeed(runtime.Status(id,observed)&&observed.systemCalls==initial.systemCalls+1&&observed.observedCs==0x23&&observed.observedCr3==cr3,"actual syscall accounting");
            FileNeed(cpu->ebx==before.ebx&&cpu->ecx==before.ecx&&cpu->edx==before.edx&&cpu->esi==before.esi&&cpu->edi==before.edi
                &&cpu->ebp==before.ebp&&cpu->ds==before.ds&&cpu->es==before.es&&cpu->fs==before.fs&&cpu->gs==before.gs,"write preserves ABI registers");
            if (diagnostic) {
                diagnosticCalls[id]++;
                // Explicit return-fault control AFTER an actual successful write.
                // This models uncertain output; it is not a disk or kernel failure.
                if (record.command) { cpu->eax=record.command==1?0xfffffffbU:record.command==2?0U:bytes+1;injectedResults[id]++; }
            } else panicCalls[id]++;
            return returned;
        }
    };
    uint32_t Cost(const Elf32LoadPlan& plan) {
        uint32_t seen[32]={},tables=0;
        for (uint32_t i=0;i<plan.segmentCount;i++) {
            const Elf32LoadSegment& s=plan.segments[i];if (!s.memorySize) continue;
            for (uint32_t di=s.virtualAddress>>22;di<=(s.virtualAddress+s.memorySize-1)>>22;di++) {
                const uint32_t bit=1U<<(di&31);if (!(seen[di>>5]&bit)) { seen[di>>5]|=bit;tables++; }
            }
        }
        const uint32_t di=NativeRuntime::UserStackBottom>>22;if (!(seen[di>>5]&(1U<<(di&31)))) tables++;
        return plan.pageCount+2+tables+1;
    }
    uint32_t Admit(NativeRuntime& runtime,const MultibootModule& module,Elf32LoadPlan& plan) {
        InterruptGuard guard;
        FileNeed(module.end>module.start&&ValidateElf32((const uint8_t*)module.start,module.end-module.start,plan),"real bounded ELF32 admission");
        uint32_t id;FileNeed(runtime.CreateElf((const uint8_t*)module.start,module.end-module.start,id),"CreateElf");
        FileNeed(id&&id<16&&idCount<16,"bounded independent owner IDs");
        for (uint32_t i=0;i<idCount;i++) FileNeed(ids[i]!=id,"owner IDs never reused");
        ids[idCount++]=id;return id;
    }
    NativeStatus Wait(NativeRuntime& runtime,TaskManager& tasks,uint32_t id,uint32_t stage,bool stop) {
        const uint32_t begin=tasks.Ticks();
        for (;;) {
            NativeStatus status={};FileRecord record;
            { InterruptGuard guard;FileNeed(runtime.Status(id,status),"live/retained status");record=Record(runtime,id); }
            if (record.error||(!status.live&&status.exitCode==91)) {
                printf((char*)"CXA VIRTUAL USER ERROR");Value(" stage=",record.stage);Value(" error=",record.error);Value(" checks=",record.checks);
                Value(" line=",record.ack);Value(" exit=",status.exitCode);Value(" vector=",status.faultVector);
                Value(" actual=",record.file);Value(" expected=",record.invalid_io);Value(" actual_hi=",record.directory);Value(" expected_hi=",record.foreign);
                Value(" mode=",record.command);Value(" sticky=",record.png_bytes);Value(" cw=",record.base);Value(" mxcsr=",record.vm_handle);Value(" sw=",record.progress);printf((char*)"\n");FileFinish(false);
            }
            if (stop?!status.live:record.stage==stage&&record.progress) return status;
            FileNeed(!record.error&&status.live&&tasks.Ticks()-begin<5000,"bounded actual float suite/continuation");WaitTicks(tasks,1);
        }
    }
}
asm(".section .text.native_float_unused_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
#endif
