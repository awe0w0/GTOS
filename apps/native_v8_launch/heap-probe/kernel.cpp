#include <process/native_runtime.h>
#include <memory/criticalsection.h>
#include <hardwarecommunication/interrupts.h>
#include <syscalls.h>
#include <process/clock_abi.h>
#include "record.h"
using namespace gtos;
using namespace gtos::memory;
using namespace gtos::process;
using namespace gtos::hardwarecommunication;
extern "C" uint8_t kernel_start,kernel_end,kernel_readonly_start,kernel_readonly_end;
void printf(char* text) { while (*text) { asm volatile("outb %0,$0xe9"::"a"(*text));++text; } }
void printfHex32(uint32_t value) {
    for (int shift=28;shift>=0;shift-=4) { char digit="0123456789ABCDEF"[(value>>shift)&15];asm volatile("outb %0,$0xe9"::"a"(digit)); }
}
void printfHex(uint8_t value) { printfHex32(value); }
void operator delete(void*) {}
namespace {
    volatile uint32_t peerProgress;
    uint32_t checks;
    void Peer() { for (;;) ++peerProgress; }
    [[noreturn]] void Finish(bool pass) {
        printf(pass?(char*)"GTOS LARGE HEAP CASES PASS\n":(char*)"GTOS LARGE HEAP CASES FAIL\n");
        asm volatile("outb %0,$0xf4"::"a"((uint8_t)(pass?0x10:0x11)));for (;;) asm volatile("cli; hlt");
    }
    void Need(bool pass,const char* why) { ++checks;if (!pass) { printf((char*)"FAILED ");printf((char*)why);printf((char*)"\n");Finish(false); } }
    void Value(const char* name,uint32_t value) { printf((char*)name);printfHex32(value); }
}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"GTOS LARGE HEAP CASES BOOT\n");
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;Need(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical map");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    Need(paging.prepareIdentity(frames,config),"kernel template");NativeRuntime runtime;
    Need(runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors()
        &&runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2),"real native runtime");
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;Need((boot->flags&8)&&boot->moduleCount==1,"one actual allocator consumer");
    const MultibootModule& module=*(const MultibootModule*)boot->modules;
    Elf32LoadPlan plan={};Need(ValidateElf32((const uint8_t*)module.start,module.end-module.start,plan),"actual consumer ELF");
    uint32_t offset=0;
    for (uint32_t i=0;i<plan.segmentCount;i++) if (plan.segments[i].virtualAddress==HEAP_RECORD_VA) offset=plan.segments[i].fileOffset;
    Need(offset && offset<=module.end-module.start-sizeof(HeapRecord),"bounded consumer record");
    const uint32_t baseline=frames.getStatistics().freeFrames;
    Task peer(&gdt,Peer);Need(tasks.AddTask(&peer),"kernel peer");
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate();
    for (uint32_t mode=0;mode<5;mode++) {
        HeapRecord fresh={1,mode,0,0,0,0};
        for (uint32_t i=0;i<sizeof(fresh);i++) ((uint8_t*)module.start)[offset+i]=((const uint8_t*)&fresh)[i];
        uint32_t id=0;Need(runtime.CreateElf((const uint8_t*)module.start,module.end-module.start,id)&&id,"default-budget process");
        const uint32_t start=tasks.BootTicks();NativeStatus status={};HeapRecord record={};
        for (;;) {
            { InterruptGuard guard;Need(runtime.Status(id,status)&&runtime.ReadMemory(id,HEAP_RECORD_VA,&record,sizeof(record)),"actual consumer state");
              if (!status.live) break;
              Need(tasks.BootTicks()-start<5000,"bounded consumer progress");
              if (mode==4&&record.stage==10) Need(runtime.RequestExit(id,0x74),"external cancellation with live allocations"); }
            asm volatile("sti; hlt":::"memory");
        }
        { InterruptGuard guard;
          printf((char*)"LARGE HEAP STOP");Value(" mode=",mode);Value(" stage=",record.stage);Value(" error=",record.error);Value(" exit=",status.exitCode);printf((char*)"\n");
          Need(!record.error&&status.observedCs==0x23&&status.observedCr3==status.directory&&peerProgress,"real consumer assertions and CPL3");
          Need(mode==0 ? record.stage==20&&status.exitCode==0 : record.stage==10
            &&status.exitCode==(mode<3?0x48000002U:mode==3?0x80000006U:0x74U),"normal/bad-pointer/fault/cancel result");
          Need(status.faultVector==(mode==3?6U:0U),"expected fault vector");
          ProcessMemoryStatistics memory={};Need(runtime.MemoryStatistics(id,memory)&&memory.capacityPages==256,"default quota retained");
          Need(mode==0 ? memory.dynamicPages==0&&memory.residentPages==plan.pageCount+2 : memory.dynamicPages>0,"allocator release versus retained fault resources");
          if (mode==3||mode==4) { uint8_t byte=0;Need(runtime.ReadMemory(id,record.held+69999,&byte,1)&&byte==0x5a,"retained independent mapping contents"); }
          Need(runtime.Reap()==1&&runtime.Reap()==0&&frames.getStatistics().freeFrames==baseline,"all arena/large pages and tables reaped");
          const NativeFpStatistics fp=runtime.FpStatistics();Need(fp.initialized==mode+1&&fp.invalidated==mode+1&&!fp.invariantFailures,"FP ownership reclaimed");
          printf((char*)"LARGE HEAP CASE");Value(" mode=",mode);Value(" checks=",record.checks);Value(" dynamic=",memory.dynamicPages);
          Value(" free=",frames.getStatistics().freeFrames);Value(" baseline=",baseline);printf((char*)"\n");
        }
    }
    Value("LARGE HEAP KERNEL CHECKS=",checks);printf((char*)"\n");Finish(true);
}
