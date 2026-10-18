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
    struct ClipboardRecord {
        unsigned version,mode,stage,error,checks,mask,result,constructors;
        unsigned format_cases,serialization_cases,comparison_cases,lifetime_waves;
        unsigned custom_cases,web_custom_cases,retained_bytes,ascii_rejection_attempts;
        unsigned reserved[48];
    };
    static_assert(sizeof(unsigned)==4&&sizeof(ClipboardRecord)==256,"fixed clipboard wire record");
    volatile uint32_t peerProgress;
    uint32_t checks;
    void Peer() { for (;;) ++peerProgress; }
    [[noreturn]] void Finish(bool pass) {
        printf(pass?(char*)"GTOS CLIPBOARD RUN PASS\n":(char*)"GTOS CLIPBOARD RUN FAIL\n");
        asm volatile("outl %0,%1"::"a"(pass?0x10U:0x20U),"Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void Need(bool pass,const char* why) {
        ++checks;
        if (!pass) { asm volatile("cli":::"memory"); printf((char*)"FAILED CLIPBOARD "); printf((char*)why); printf((char*)"\n"); Finish(false); }
    }
    void Value(const char* name,uint32_t value) { printf((char*)name); printfHex32(value); }
    void WaitTicks(TaskManager& tasks,uint32_t ticks) {
        const uint32_t start=tasks.BootTicks();
        while ((uint32_t)(tasks.BootTicks()-start)<ticks) asm volatile("sti; hlt":::"memory");
    }
}
asm(".section .text.native_clipboard_unused_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"GTOS CLIPBOARD BOOT\n");
    GlobalDescriptorTable gdt; TaskManager tasks; InterruptsManager interrupts(0x20,&gdt,&tasks); SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames; Need(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical map");
    KernelPaging paging; PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    static NativeRuntime runtime __attribute__((no_destroy));
    Need(paging.prepareIdentity(frames,config)&&runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors()
        &&runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2),"current canonical runtime with real constructor");
    const MultibootInfo* boot=(const MultibootInfo*)multiboot; Need((boot->flags&8)&&boot->moduleCount==1,"one actual clipboard ELF module");
    const MultibootModule& module=*(const MultibootModule*)boot->modules;
    Elf32LoadPlan plan={}; Elf32Error error; Need(ValidateElf32((const uint8_t*)module.start,module.end-module.start,plan,error,4096),"clipboard ELF admission");
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
          Need(runtime.CreateElf((const uint8_t*)module.start,module.end-module.start,id,profile)&&id,"unchanged trusted clipboard profile");
          Need(runtime.MemoryStatistics(id,memory)&&memory.residentPages==plan.pageCount+128
              &&memory.capacityPages==plan.pageCount+128+1024&&!memory.dynamicPages&&memory.metadataFrames,"initial measured image/stack/runtime budget");
          const uint32_t bottom=NativeRuntime::UserStackTop-128*4096; uint32_t probe=0xffffffffU; ClipboardRecord fresh={};
          Need(runtime.ReadMemory(id,bottom,&probe,4)&&!probe&&!runtime.ReadMemory(id,bottom-4,&probe,4)
              &&!runtime.ReadMemory(id,NativeRuntime::UserStackTop,&probe,4),"zero stack and both absent guards");
          Need(runtime.ReadMemory(id,CLIPBOARD_RECORD_VA,&fresh,sizeof(fresh))&&fresh.version==1&&fresh.mode==CLIPBOARD_EXPECT_NEGATIVE
              &&!fresh.stage&&!fresh.error&&!fresh.checks&&!fresh.mask&&fresh.result==0xffffffffU&&!fresh.constructors
              &&!fresh.format_cases&&!fresh.serialization_cases&&!fresh.comparison_cases&&!fresh.lifetime_waves
              &&!fresh.custom_cases&&!fresh.web_custom_cases&&!fresh.retained_bytes&&!fresh.ascii_rejection_attempts,"fresh independently compiled mode and exact clipboard record");
          for (unsigned i=0;i<48;++i) Need(!fresh.reserved[i],"fresh reserved fields");
          printf((char*)"CLIPBOARD INITIAL"); Value(" wave=",wave); Value(" image=",plan.pageCount); Value(" resident=",memory.residentPages);
          Value(" capacity=",memory.capacityPages); Value(" metadata=",memory.metadataFrames); Value(" tables=",memory.tableFrames); printf((char*)"\n"); }
        NativeStatus status={}; bool cancelled=false;
        for (;;) {
            { InterruptGuard guard; Need(runtime.Status(id,status),"actual clipboard status"); if (!status.live) break;
              if (!cancelled&&(uint32_t)(tasks.BootTicks()-bootBefore)>5000) { Need(runtime.RequestExit(id,0x74),"bounded clipboard cancellation"); cancelled=true; } }
            asm volatile("sti; hlt":::"memory");
        }
        { InterruptGuard guard; ClipboardRecord record={}; uint32_t exitStage=0,exitResult=0;
          Need(runtime.ReadMemory(id,CLIPBOARD_RECORD_VA,&record,sizeof(record))&&runtime.ReadMemory(id,CLIPBOARD_EXIT_STAGE_VA,&exitStage,4)
              &&runtime.ReadMemory(id,CLIPBOARD_EXIT_RESULT_VA,&exitResult,4),"actual clipboard record and real exit markers");
          Need(runtime.MemoryStatistics(id,memory),"stopped resident statistics");
          printf((char*)"CLIPBOARD EXECUTION"); Value(" wave=",wave); Value(" mode=",record.mode); Value(" stage=",record.stage); Value(" result=",record.result); Value(" error=",record.error);
          Value(" exit=",status.exitCode); Value(" vector=",status.faultVector); Value(" fault=",status.faultAddress); Value(" checks=",record.checks);
          Value(" calls=",status.systemCalls); Value(" dynamic=",memory.dynamicPages); Value(" mask=",record.mask); Value(" cs=",status.observedCs);
          Value(" ctor=",record.constructors); Value(" exitstage=",exitStage); Value(" exitresult=",exitResult); printf((char*)"\n");
          Need(!cancelled&&!status.live&&!status.reaped&&status.observedCs==0x23&&status.observedCr3==status.directory&&status.directory!=kernelCr3
              &&record.version==1&&record.mode==CLIPBOARD_EXPECT_NEGATIVE&&record.constructors==CLIPBOARD_CTOR_COUNT
              &&!status.faultVector&&!status.faultError&&!status.faultAddress,"real CPL3 constructor and exit lifecycle without faults");
          for (unsigned i=0;i<48;++i) Need(!record.reserved[i],"stopped reserved fields");
#if CLIPBOARD_EXPECT_NEGATIVE
          Need(record.stage==10&&record.result==0xffffffffU&&!record.error&&!record.checks&&!record.mask
              &&!record.format_cases&&!record.serialization_cases&&!record.comparison_cases&&!record.lifetime_waves
              &&!record.custom_cases&&!record.web_custom_cases&&!record.retained_bytes&&record.ascii_rejection_attempts==1
              &&status.exitCode==0x48000021U&&!exitStage&&exitResult==0xffffffffU,"actual non-ASCII CHECK invokes genuine abort and heap panic SYS_EXIT");
          printf((char*)"CLIPBOARD ASCII CHECK REJECTED\n");
#else
          Need(record.stage==3&&!record.result&&!record.error&&!status.exitCode&&exitStage==4&&!exitResult
              &&record.checks==289&&record.mask==63&&record.format_cases==12&&record.serialization_cases==14
              &&record.comparison_cases==100&&record.lifetime_waves==32&&record.custom_cases==4&&record.web_custom_cases==4
              &&record.retained_bytes==66272&&!record.ascii_rejection_attempts,"whole real format identity serialization comparison and string ownership semantics");
          printf((char*)"CLIPBOARD SEMANTICS"); Value(" wave=",wave); Value(" formats=",record.format_cases); Value(" serialized=",record.serialization_cases);
          Value(" comparisons=",record.comparison_cases); Value(" lifetimes=",record.lifetime_waves); Value(" custom=",record.custom_cases);
          Value(" webcustom=",record.web_custom_cases); Value(" retained=",record.retained_bytes); Value(" rejected=",record.ascii_rejection_attempts); printf((char*)"\n");
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
          printf((char*)"CLIPBOARD REAP"); Value(" wave=",wave); Value(" free=",frames.getStatistics().freeFrames); Value(" baseline=",baseline);
          Value(" tasks=",tasks.TaskCount()); Value(" fp_init=",fpAfter.initialized); Value(" fp_invalid=",fpAfter.invalidated);
          Value(" fp_failures=",fpAfter.invariantFailures); Value(" checks=",checks); printf((char*)"\n"); }
        WaitTicks(tasks,8); Need(peerProgress!=peerBefore&&tasks.BootTicks()>bootBefore,"kernel peer/timer continue after teardown");
    }
    Finish(true);
}
