#include <process/native_runtime.h>
#include <memory/criticalsection.h>
#include <hardwarecommunication/interrupts.h>
#include <syscalls.h>
#include <process/clock_abi.h>
using namespace gtos;
using namespace gtos::memory;
using namespace gtos::process;
using namespace gtos::hardwarecommunication;
extern "C" uint8_t kernel_start,kernel_end,kernel_readonly_start,kernel_readonly_end;
void printf(char* text) { while (*text) { asm volatile("outb %0,$0xe9"::"a"(*text)); ++text; } }
void printfHex32(uint32_t value) {
    for (int shift=28;shift>=0;shift-=4) { char digit="0123456789ABCDEF"[(value>>shift)&15]; asm volatile("outb %0,$0xe9"::"a"(digit)); }
}
void printfHex(uint8_t value) { printfHex32(value); }
void operator delete(void*) {}
namespace {
    // Same fixed 16 unsigned fields and 48 reserved words as the user record.
    struct ClassRecord {
        unsigned version,mode,stage,error,checks,mask,result,constructors;
        unsigned string_cases,path_cases,lifetime_waves,pma_allocations;
        unsigned mapping_handle,mapping_base,mapping_pages,mapping_released;
        unsigned reserved[48];
    };
    static_assert(sizeof(unsigned)==4&&sizeof(ClassRecord)==256,"fixed class wire record");
    volatile uint32_t peerProgress;
    uint32_t checks;
    void Peer() { for (;;) ++peerProgress; }
    [[noreturn]] void Finish(bool pass) {
        printf(pass?(char*)"GTOS CLASS RUN PASS\n":(char*)"GTOS CLASS RUN FAIL\n");
        asm volatile("outl %0,%1"::"a"(pass?0x10U:0x20U),"Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void Need(bool pass,const char* why) {
        ++checks;
        if (!pass) { asm volatile("cli":::"memory"); printf((char*)"FAILED CLASS "); printf((char*)why); printf((char*)"\n"); Finish(false); }
    }
    void Value(const char* name,uint32_t value) { printf((char*)name); printfHex32(value); }
    void WaitTicks(TaskManager& tasks,uint32_t ticks) {
        const uint32_t start=tasks.BootTicks();
        while ((uint32_t)(tasks.BootTicks()-start)<ticks) asm volatile("sti; hlt":::"memory");
    }
}
asm(".section .text.native_class_unused_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"GTOS CLASS BOOT\n");
    GlobalDescriptorTable gdt; TaskManager tasks; InterruptsManager interrupts(0x20,&gdt,&tasks); SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames; Need(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical map");
    KernelPaging paging; PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    static NativeRuntime runtime __attribute__((no_destroy));
    Need(paging.prepareIdentity(frames,config)&&runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors()
        &&runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2),"current canonical runtime with real constructor");
    const MultibootInfo* boot=(const MultibootInfo*)multiboot; Need((boot->flags&8)&&boot->moduleCount==1,"one actual class ELF module");
    const MultibootModule& module=*(const MultibootModule*)boot->modules;
    Elf32LoadPlan plan={}; Elf32Error error; Need(ValidateElf32((const uint8_t*)module.start,module.end-module.start,plan,error,4096),"class ELF admission");
    const uint32_t baseline=frames.getStatistics().freeFrames,kernelCr3=paging.getStatistics().directoryAddress;
    Task peer(&gdt,Peer); Need(tasks.AddTask(&peer),"independent kernel peer");
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));
    asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate(); WaitTicks(tasks,8); uint32_t stoppedDynamic=0;
    for (uint32_t wave=0;wave<2;++wave) {
        const auto fpBefore=runtime.FpStatistics(); const auto statsBefore=runtime.Statistics();
        const uint32_t peerBefore=peerProgress,bootBefore=tasks.BootTicks(); uint32_t id=0;
        const NativeElfLaunchProfile profile={4096,128,1024}; ProcessMemoryStatistics memory={};
        { InterruptGuard guard;
          Need(runtime.CreateElf((const uint8_t*)module.start,module.end-module.start,id,profile)&&id,"unchanged trusted class profile");
          Need(runtime.MemoryStatistics(id,memory)&&memory.residentPages==plan.pageCount+128
              &&memory.capacityPages==plan.pageCount+128+1024&&!memory.dynamicPages&&memory.metadataFrames,"initial measured image/stack/runtime budget");
          const uint32_t bottom=NativeRuntime::UserStackTop-128*4096; uint32_t probe=0xffffffffU; ClassRecord fresh={};
          Need(runtime.ReadMemory(id,bottom,&probe,4)&&!probe&&!runtime.ReadMemory(id,bottom-4,&probe,4)
              &&!runtime.ReadMemory(id,NativeRuntime::UserStackTop,&probe,4),"zero stack and both absent guards");
          Need(runtime.ReadMemory(id,CLASS_RECORD_VA,&fresh,sizeof(fresh))&&fresh.version==1&&!fresh.stage&&!fresh.error&&!fresh.mask&&!fresh.constructors,"fresh actual class record");
          printf((char*)"CLASS INITIAL"); Value(" wave=",wave); Value(" image=",plan.pageCount); Value(" resident=",memory.residentPages);
          Value(" capacity=",memory.capacityPages); Value(" metadata=",memory.metadataFrames); Value(" tables=",memory.tableFrames); printf((char*)"\n"); }
        NativeStatus status={}; bool cancelled=false;
        for (;;) {
            { InterruptGuard guard; Need(runtime.Status(id,status),"actual class status"); if (!status.live) break;
              if (!cancelled&&(uint32_t)(tasks.BootTicks()-bootBefore)>5000) { Need(runtime.RequestExit(id,0x74),"bounded class cancellation"); cancelled=true; } }
            asm volatile("sti; hlt":::"memory");
        }
        { InterruptGuard guard; ClassRecord record={}; uint32_t exitStage=0,exitResult=0;
          Need(runtime.ReadMemory(id,CLASS_RECORD_VA,&record,sizeof(record))&&runtime.ReadMemory(id,CLASS_EXIT_STAGE_VA,&exitStage,4)
              &&runtime.ReadMemory(id,CLASS_EXIT_RESULT_VA,&exitResult,4),"actual class record and real exit markers");
          Need(runtime.MemoryStatistics(id,memory),"stopped resident statistics");
          printf((char*)"CLASS EXECUTION"); Value(" wave=",wave); Value(" stage=",record.stage); Value(" result=",record.result); Value(" error=",record.error);
          Value(" exit=",status.exitCode); Value(" vector=",status.faultVector); Value(" fault=",status.faultAddress); Value(" checks=",record.checks);
          Value(" calls=",status.systemCalls); Value(" dynamic=",memory.dynamicPages); Value(" mask=",record.mask); Value(" cs=",status.observedCs);
          Value(" ctor=",record.constructors); Value(" exitstage=",exitStage); printf((char*)"\n");
          Need(!cancelled&&!status.live&&!status.reaped&&status.observedCs==0x23&&status.observedCr3==status.directory&&status.directory!=kernelCr3
              &&record.version==1&&record.stage==3&&record.constructors==CLASS_CTOR_COUNT&&exitStage==4&&exitResult==record.result
              &&!status.faultVector&&!status.faultError&&!status.faultAddress,"real CPL3 constructor/class/exit lifecycle without faults");
#if CLASS_EXPECT_NEGATIVE
          Need(record.result==CLASS_EXPECT_ERROR&&record.error==CLASS_EXPECT_ERROR&&status.exitCode==CLASS_EXPECT_ERROR
              &&!record.mask&&!record.string_cases&&!record.path_cases&&!record.mapping_handle,"wrong diagnostic length rejected at unmapped max0 assertion");
          printf((char*)"CLASS WRONG LENGTH REJECTED\n");
#else
          Need(!record.result&&!record.error&&!status.exitCode&&record.mask==7&&record.string_cases==14&&record.path_cases==21
              &&record.lifetime_waves==32&&record.pma_allocations>2&&record.pma_allocations<1026&&record.mapping_handle
              &&record.mapping_base>=0x80000000U&&record.mapping_pages==257&&record.mapping_released==1,"actual string guards, lexical UTF lifetime and full LocalPMA semantics");
          printf((char*)"CLASS SEMANTICS"); Value(" wave=",wave); Value(" strings=",record.string_cases); Value(" paths=",record.path_cases);
          Value(" lifetimes=",record.lifetime_waves); Value(" allocations=",record.pma_allocations); Value(" pages=",record.mapping_pages); Value(" released=",record.mapping_released); printf((char*)"\n");
#endif
          Need(memory.dynamicPages<=1024&&memory.residentPages==plan.pageCount+128+memory.dynamicPages,"bounded stopped address-space residency");
          if (wave) Need(memory.dynamicPages==stoppedDynamic,"no retained page growth across launches");
          stoppedDynamic=memory.dynamicPages;
          Need(runtime.Reap()==1&&frames.getStatistics().freeFrames==baseline&&tasks.TaskCount()==1&&runtime.Reap()==0,"exact image/heap/stack/metadata/task recovery");
          const auto fpAfter=runtime.FpStatistics(); const auto statsAfter=runtime.Statistics();
          Need(fpAfter.initialized-fpBefore.initialized==1&&fpAfter.invalidated-fpBefore.invalidated==1&&!fpAfter.invariantFailures
              &&fpAfter.saves>=fpBefore.saves&&fpAfter.restores>=fpBefore.restores,"FP context exactly released");
          Need(statsAfter.created-statsBefore.created==1&&statsAfter.exited-statsBefore.exited==1
              &&statsAfter.reaped-statsBefore.reaped==1&&statsAfter.faulted==statsBefore.faulted,"exact process lifecycle counts");
          printf((char*)"CLASS REAP"); Value(" wave=",wave); Value(" free=",frames.getStatistics().freeFrames); Value(" baseline=",baseline);
          Value(" tasks=",tasks.TaskCount()); Value(" fp_init=",fpAfter.initialized); Value(" fp_invalid=",fpAfter.invalidated);
          Value(" fp_failures=",fpAfter.invariantFailures); Value(" checks=",checks); printf((char*)"\n"); }
        WaitTicks(tasks,8); Need(peerProgress!=peerBefore&&tasks.BootTicks()>bootBefore,"kernel peer/timer continue after teardown");
    }
    Finish(true);
}
