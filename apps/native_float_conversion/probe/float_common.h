#ifndef GTOS_NATIVE_FLOAT_KERNEL_COMMON_H
#define GTOS_NATIVE_FLOAT_KERNEL_COMMON_H
#include "fixture-helpers.h"
#include <memory/criticalsection.h>
#include <process/clock_abi.h>
#include "record.h"
namespace {
    uint32_t fileChecks,ids[16],idCount,kernelDirectory,maxTrapStack;
    void FileFinish(bool pass) __attribute__((noreturn));
    void FileFinish(bool pass) {
        printf(pass?(char*)"NATIVE FLOAT GUEST PASS\n":(char*)"NATIVE FLOAT GUEST FAIL\n");
        asm volatile("outl %0,%1"::"a"(pass?0x10U:0x20U),"Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void FileNeed(bool pass,const char* why) {
        fileChecks++;
        if (!pass) { asm volatile("cli":::"memory");printf((char*)"FAILED FLOAT ");printf((char*)why);printf((char*)"\n");FileFinish(false); }
    }
    void Value(const char* name,uint32_t x) { printf((char*)name);printfHex32(x); }
    FileRecord Record(NativeRuntime& r,uint32_t id) {
        FileRecord x={};FileNeed(r.ReadMemory(id,FILE_RECORD_VA,&x,sizeof(x)),"actual user float record");return x;
    }
    class FloatTap:public InterruptHandler {
        SyscallHandler& original;
    public:
        FloatTap(InterruptsManager& i,SyscallHandler& o):InterruptHandler(&i,0x80),original(o) {}
        uint32_t HandlerInterrupt(uint32_t esp) {
            CPUState* cpu=(CPUState*)esp;uint32_t sp;asm volatile("mov %%esp,%0":"=r"(sp));
            FileNeed(cpu->cs==0x23&&sp>=NativeRuntime::KernelStackArena&&sp<NativeRuntime::KernelStackArena+0x15000,"actual CPL3 syscall on bounded trap stack");
            const uint32_t index=(sp-NativeRuntime::KernelStackArena)/(5*4096);
            const uint32_t used=NativeRuntime::KernelStackArena+(index+1)*5*4096-sp;
            if (used>maxTrapStack) maxTrapStack=used;
            FileNeed(used<4*4096-512,"observed trap entry stack margin");
            FileNeed(cpu->eax<0x4720||cpu->eax>0x472f,"float fixture has no file operations");
            return original.HandlerInterrupt(esp);
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
                printf((char*)"FLOAT USER ERROR");Value(" stage=",record.stage);Value(" error=",record.error);Value(" checks=",record.checks);
                Value(" line=",record.ack);Value(" exit=",status.exitCode);Value(" vector=",status.faultVector);
                Value(" actual=",record.file);Value(" expected=",record.invalid_io);Value(" cw=",record.directory);Value(" mxcsr=",record.foreign);
                Value(" backend=",record.command);Value(" expected_mode=",record.png_bytes);Value(" mode=",record.progress);printf((char*)"\n");FileFinish(false);
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
