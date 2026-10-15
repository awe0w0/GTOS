#include <process/native_runtime.h>
#include <memory/criticalsection.h>
#include <hardwarecommunication/interrupts.h>
#include <syscalls.h>
#include <process/clock_abi.h>
#include "entry-symbols.h"
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
        printf(pass?(char*)"GTOS REAL V8 LARGE HEAP RUN PASS\n":(char*)"GTOS REAL V8 LARGE HEAP RUN FAIL\n");
        asm volatile("outb %0,$0xf4"::"a"((uint8_t)(pass?0x10:0x11)));for (;;) asm volatile("cli; hlt");
    }
    void Need(bool pass,const char* why) { ++checks;if (!pass) { printf((char*)"FAILED ");printf((char*)why);printf((char*)"\n");Finish(false); } }
    void Value(const char* name,uint32_t value) { printf((char*)name);printfHex32(value); }
}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"GTOS REAL V8 LARGE HEAP BOOT\n");
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;Need(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical map");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    Need(paging.prepareIdentity(frames,config),"kernel template");NativeRuntime runtime;
    Need(runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors()
        &&runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2),"real native runtime");
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;Need((boot->flags&8)&&boot->moduleCount==1,"one actual full59 module");
    const MultibootModule& module=*(const MultibootModule*)boot->modules;
    Elf32LoadPlan plan={};Elf32Error error;Need(ValidateElf32((const uint8_t*)module.start,module.end-module.start,plan,error,4096),"actual current ELF plan");
    const uint32_t baseline=frames.getStatistics().freeFrames;uint32_t id=0;
    const NativeElfLaunchProfile profile={4096,128,1024};
    Need(runtime.CreateElf((const uint8_t*)module.start,module.end-module.start,id,profile)&&id,"actual full V8 launch profile admitted");
    ProcessMemoryStatistics memory={};Need(runtime.MemoryStatistics(id,memory)&&memory.residentPages==plan.pageCount+128
        &&memory.capacityPages==plan.pageCount+128+1024,"measured image/stack/runtime budget");
    const uint32_t stackBottom=NativeRuntime::UserStackTop-128*4096;uint32_t probe=0xffffffffU;
    Need(runtime.ReadMemory(id,stackBottom,&probe,4)&&probe==0
        &&!runtime.ReadMemory(id,stackBottom-4,&probe,4)
        &&!runtime.ReadMemory(id,NativeRuntime::UserStackTop,&probe,4),"zero root stack and both absent guards");
    printf((char*)"V8 INITIAL");Value(" image=",plan.pageCount);Value(" resident=",memory.residentPages);Value(" capacity=",memory.capacityPages);
    Value(" metadata=",memory.metadataFrames);Value(" tables=",memory.tableFrames);printf((char*)"\n");
    Task peer(&gdt,Peer);Need(tasks.AddTask(&peer),"kernel peer");
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate();const uint32_t start=tasks.BootTicks();NativeStatus status={};bool cancelled=false;
    for (;;) {
        { InterruptGuard guard;Need(runtime.Status(id,status),"actual status");if (!status.live) break;
          if (!cancelled&&(uint32_t)(tasks.BootTicks()-start)>5000) { Need(runtime.RequestExit(id,0x74),"bounded external cancellation");cancelled=true; } }
        asm volatile("sti; hlt":::"memory");
    }
    { InterruptGuard guard;uint32_t stage=0,result=0;
      Need(runtime.ReadMemory(id,ENTRY_STAGE_VA,&stage,4)&&runtime.ReadMemory(id,ENTRY_RESULT_VA,&result,4),"actual entry state");
      Need(runtime.MemoryStatistics(id,memory),"stopped resident statistics");
      Need(stage>=1&&status.observedCs==0x23&&status.observedCr3==status.directory&&peerProgress,"real CPL3 execution and peer");
      printf((char*)"V8 EXECUTION");Value(" stage=",stage);Value(" result=",result);Value(" exit=",status.exitCode);
      Value(" vector=",status.faultVector);Value(" error=",status.faultError);Value(" fault=",status.faultAddress);
      Value(" calls=",status.systemCalls);Value(" resident=",memory.residentPages);Value(" dynamic=",memory.dynamicPages);Value(" peer=",peerProgress);printf((char*)"\n");
      struct Large { uint32_t base,handle,length,requested,alignment; } allocations[16]={};
      Need(runtime.ReadMemory(id,LARGE_ALLOCS_VA,allocations,sizeof(allocations)),"actual independent allocation metadata");
      uint32_t count=0,pages=0;
      for (uint32_t i=0;i<16;i++) if (allocations[i].handle) {
          const Large& block=allocations[i];
          Need(block.base>=0x80000000U&&block.length&&block.length<=1024*1024
            &&!(block.length&4095)&&block.base<=0xbfffc000U-block.length&&block.requested&&block.requested<=block.length
            &&block.alignment>=16&&!(block.alignment&(block.alignment-1))&&!(block.base&(block.alignment-1)),"actual independent block bounds");
          ++count;pages+=block.length/4096;
          printf((char*)"V8 LARGE BLOCK");Value(" index=",i);Value(" base=",block.base);Value(" pages=",block.length/4096);
          Value(" requested=",block.requested);Value(" alignment=",block.alignment);printf((char*)"\n");
      }
      printf((char*)"V8 LARGE TOTAL");Value(" blocks=",count);Value(" pages=",pages);printf((char*)"\n");
      Need(runtime.Reap()==1&&frames.getStatistics().freeFrames==baseline,"image/heap/metadata exact reap");
      printf((char*)"V8 REAP");Value(" free=",frames.getStatistics().freeFrames);Value(" baseline=",baseline);Value(" checks=",checks);printf((char*)"\n");
    }
    Finish(true);
}
