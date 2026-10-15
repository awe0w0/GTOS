#include <process/native_runtime.h>
#include <memory/criticalsection.h>
#include <hardwarecommunication/interrupts.h>
#include <syscalls.h>
#include <process/clock_abi.h>
#include <process/info_abi.h>
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
    uint32_t held[16384],heldCount,checks;
    class InfoTap : public SyscallHandler {
    public:
        uint32_t count,limit;
        bool consistent;
        InfoTap(InterruptsManager* interrupts) : SyscallHandler(interrupts,0x80),count(0),limit(0),consistent(true) {}
        uint32_t HandlerInterrupt(uint32_t esp) override {
            CPUState* cpu=(CPUState*)esp;const uint32_t operation=cpu->eax,request=cpu->ebx;
            const uint32_t result=SyscallHandler::HandlerInterrupt(esp);
            if (operation==GTOS_SYS_PROCESS_INFO&&result==esp&&cpu->eax==0) {
                // Success validated both complete wire buffers; IF remains clear
                // and this synchronous syscall retains the caller's CR3.
                const GtosProcessInfoRequest* input=(const GtosProcessInfoRequest*)request;
                const GtosProcessInfoResult* output=(const GtosProcessInfoResult*)input->result_va;
                if (count&&limit!=output->maximum_pages) consistent=false;
                limit=output->maximum_pages;++count;
            }
            return result;
        }
    };
    volatile uint32_t peerProgress;
    void Peer() { for (;;) ++peerProgress; }
    [[noreturn]] void Finish(bool pass) {
        printf(pass?(char*)"GTOS LARGE V8 ADMISSION PASS\n":(char*)"GTOS LARGE V8 ADMISSION FAIL\n");
        asm volatile("outb %0,$0xf4"::"a"((uint8_t)(pass?0x10:0x11)));for (;;) asm volatile("cli; hlt");
    }
    void Need(bool pass,const char* reason) { ++checks;if (!pass) { printf((char*)"FAILED ");printf((char*)reason);printf((char*)"\n");Finish(false); } }
    void Value(const char* name,uint32_t value) { printf((char*)name);printfHex32(value); }
    void HoldAll(PhysicalMemoryManager& frames) {
        heldCount=0;uint32_t address;
        while (frames.allocate(address)) { Need(heldCount<16384,"bounded64M frame inventory");held[heldCount++]=address; }
        Need(frames.getStatistics().freeFrames==0,"physical exhaustion reached");
    }
    uint32_t ReleaseRun(PhysicalMemoryManager& frames,uint32_t count) {
        for (uint32_t start=0;start+count<=heldCount;start++) {
            bool adjacent=true;for (uint32_t i=1;i<count;i++) if (held[start+i]!=held[start]+i*4096) adjacent=false;
            if (!adjacent) continue;
            for (uint32_t i=0;i<count;i++) { Need(frames.free(held[start+i]),"release controlled frame");held[start+i]=0; }
            return count;
        }
        Need(false,"controlled contiguous free run available");return 0;
    }
    void ReleaseHeld(PhysicalMemoryManager& frames) {
        for (uint32_t i=0;i<heldCount;i++) if (held[i]) Need(frames.free(held[i]),"restore held frame");
        heldCount=0;
    }
    void RollbackCases(KernelPaging& paging,PhysicalMemoryManager& frames) {
        const uint32_t baseline=frames.getStatistics().freeFrames;
        ProcessAddressSpace space;
        Need(!space.Prepare(paging,frames,255)&&space.GetLastError()==ProcessMemoryLimit
            &&!space.Prepared()&&frames.getStatistics().freeFrames==baseline,"below default metadata budget rejected");
        Need(!space.Prepare(paging,frames,ProcessAddressSpace::MaximumCapacityPages+1)
            &&space.GetLastError()==ProcessMemoryLimit&&!space.Prepared()
            &&frames.getStatistics().freeFrames==baseline,"above maximum metadata budget rejected");
        HoldAll(frames);ReleaseRun(frames,11);
        Need(!space.Prepare(paging,frames,2641)&&space.GetLastError()==ProcessMemoryNoMemory&&!space.Prepared(),"directory OOM rolls back11 metadata frames");
        Need(frames.getStatistics().freeFrames==11,"metadata allocation rolled back exactly");
        Need(space.Prepare(paging,frames)&&space.Destroy()&&frames.getStatistics().freeFrames==11,"failed large slot reusable by default profile");
        ReleaseHeld(frames);Need(frames.getStatistics().freeFrames==baseline,"prepare failure returns full baseline");
        Need(space.Prepare(paging,frames,2641),"large metadata prepared");
        ProcessMemoryStatistics info={};Need(space.QueryStatistics(info)&&info.metadataFrames==11&&info.capacityPages==2641,"measured metadata frame budget");
        HoldAll(frames);ReleaseRun(frames,3);
        Need(!space.MapNewRange(0x403ff000U,8192,true)&&space.GetLastError()==ProcessMemoryNoMemory,"PDE crossing failure after first page");
        Need(space.QueryStatistics(info)&&info.residentPages==0&&info.tableFrames==0&&frames.getStatistics().freeFrames==3,"bulk map rollback removes data and both table attempts");
        ReleaseHeld(frames);
        Need(space.MapNewRange(0x403ff000U,8192,true),"whole writable PDE crossing range");
        Need(space.QueryStatistics(info)&&info.residentPages==2&&info.tableFrames==2,"two data/two table ownership");
        const uint32_t mapped=frames.getStatistics().freeFrames;
        Need(!space.MapNewRange(0x403fe000U,8192,true)&&space.GetLastError()==ProcessMemoryConflict
            &&frames.getStatistics().freeFrames==mapped,"overlap preflight creates no prefix page");
        Need(!space.ProtectRange(0x40400000U,8192,false)&&space.GetLastError()==ProcessMemoryNotMapped,"protection hole rejected before mutation");
        uint32_t value=0x12345678,copy=0;
        Need(space.CopyToUser(0x403ff000U,&value,4)&&space.CopyToUser(0x40400000U,&value,4),"failed protection preserves both writable pages");
        Need(!space.MapNewRange(0x403fe000U,0,true)&&space.GetLastError()==ProcessMemoryBadRange
            &&!space.MapNewRange(0xbffff000U,8192,true)&&space.GetLastError()==ProcessMemoryBadRange
            &&frames.getStatistics().freeFrames==mapped,"empty and arena overflow ranges rejected atomically");
        Need(space.ProtectRange(0x403ff000U,8192,false),"whole range readonly");
        Need(!space.CopyToUser(0x403ff000U,&value,4)&&!space.CopyToUser(0x40400000U,&value,4)
            &&space.CopyFromUser(&copy,0x40400000U,4)&&copy==value,"readonly writes refused and bytes retained");
        Need(space.ProtectRange(0x403ff000U,8192,true)&&space.UnmapPage(0x403ff000U)
            &&space.UnmapPage(0x40400000U)&&space.QueryStatistics(info)&&!info.residentPages&&!info.tableFrames,"bulk permissions and single unmap compose");
        Need(space.Destroy()&&frames.getStatistics().freeFrames==baseline,"large metadata destruction exact baseline");
        printf((char*)"LARGE ROLLBACK PASS");Value(" free=",baseline);printf((char*)"\n");
    }
    void SmallReuse(NativeRuntime& runtime,TaskManager& tasks,PhysicalMemoryManager& frames,InfoTap& tap) {
        const uint8_t code[]={0xb8,0x13,0x47,0,0,0xbb,0,0x20,0,0x40,0xb9,16,0,0,0,0xcd,0x80,
            0x89,0xc3,0xb8,(uint8_t)GTOS_SYS_EXIT,(uint8_t)(GTOS_SYS_EXIT>>8),0,0,0xcd,0x80,0x0f,0x0b};
        struct Data { GtosProcessInfoRequest request;GtosProcessInfoResult result; } data={};
        data.request={GTOS_PROCESS_INFO_ABI_VERSION,0,NativeRuntime::DataAddress+sizeof(data.request),sizeof(data.result)};
        NativeImage image={code,sizeof(code),0,&data,sizeof(data)};const uint32_t baseline=frames.getStatistics().freeFrames;
        tap.count=tap.limit=0;tap.consistent=true;uint32_t id=0;
        Need(runtime.Create(image,id)&&id,"large slot reused by ordinary native process");
        NativeStatus status={};const uint32_t start=tasks.BootTicks();
        for (;;) {
            { InterruptGuard guard;Need(runtime.Status(id,status),"ordinary reuse status");if (!status.live) break;
              Need((uint32_t)(tasks.BootTicks()-start)<1000,"bounded ordinary self-query completion"); }
            asm volatile("sti; hlt":::"memory");
        }
        InterruptGuard guard;ProcessMemoryStatistics info={};
        Need(!status.exitCode&&!status.faultVector&&tap.count==1&&tap.consistent&&tap.limit==256,"ordinary self-query remains256 after large slot reuse");
        Need(runtime.MemoryStatistics(id,info)&&info.residentPages==4&&info.capacityPages==256&&!info.metadataFrames,"ordinary inline metadata restored");
        Need(runtime.Reap()==1&&frames.getStatistics().freeFrames==baseline,"ordinary reuse exact frame recovery");
        printf((char*)"SMALL REUSE PASS");Value(" capacity=",tap.limit);Value(" metadata=",info.metadataFrames);Value(" free=",baseline);printf((char*)"\n");
    }
}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"GTOS LARGE V8 BOOT\n");
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);InfoTap syscalls(&interrupts);
    PhysicalMemoryManager frames;Need(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical map");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    Need(paging.prepareIdentity(frames,config),"kernel template");NativeRuntime runtime;
    Need(runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors(),"kernel stacks and sealed paging");
    RollbackCases(paging,frames);
    Need(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2),"real native activation");
    const MultibootInfo* boot=(const MultibootInfo*)multiboot;Need((boot->flags&8)&&boot->moduleCount==1,"one actual full58 module");
    const MultibootModule& module=*(const MultibootModule*)boot->modules;const uint8_t* image=(const uint8_t*)module.start;const uint32_t bytes=module.end-module.start;
    Elf32LoadPlan plan={};Elf32Error error;
    Need(!ValidateElf32(image,bytes,plan,error)&&error==Elf32PageLimit,"ordinary validator stays bounded");
    Need(!ValidateElf32(image,bytes,plan,error,2382)&&error==Elf32PageLimit&&!plan.pageCount,"exact large ELF lower bound rejected with cleared output");
    Need(ValidateElf32(image,bytes,plan,error,2383)&&plan.pageCount==2383,"exact actual large ELF page budget accepted");
    const uint32_t baseline=frames.getStatistics().freeFrames;uint32_t id=17;
    Need(!runtime.CreateElf(image,bytes,id)&&!id&&frames.getStatistics().freeFrames==baseline,"ordinary runtime file/page limits unchanged");
    Need(!runtime.CreateElf(image,bytes,id,4097)&&!id&&frames.getStatistics().freeFrames==baseline,"invalid large profile has no effects");
    Need(runtime.CreateElf(image,bytes,id,4096)&&id,"actual full V8 image admitted");
    ProcessMemoryStatistics memory={};Need(runtime.MemoryStatistics(id,memory),"actual resident statistics");
    Need(memory.residentPages==2385&&memory.capacityPages==2641&&memory.metadataFrames==11&&memory.tableFrames==4&&!memory.dynamicPages,"image+stack+metadata+tables budget");
    printf((char*)"V8 IMAGE ADMITTED");Value(" pages=",memory.residentPages);Value(" metadata=",memory.metadataFrames);Value(" tables=",memory.tableFrames);Value(" free=",frames.getStatistics().freeFrames);Value(" baseline=",baseline);printf((char*)"\n");
    Task peer(&gdt,Peer);Need(tasks.AddTask(&peer),"kernel peer");
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate();const uint32_t start=tasks.BootTicks();NativeStatus status={};bool cancelled=false;
    for (;;) {
        { InterruptGuard guard;Need(runtime.Status(id,status),"actual process status");if (!status.live) break;
          if (!cancelled&&(uint32_t)(tasks.BootTicks()-start)>5000) { Need(runtime.RequestExit(id,0x74),"bounded external cancellation");cancelled=true; } }
        asm volatile("sti; hlt":::"memory");
    }
    uint32_t stage=0,result=0;
    { InterruptGuard guard;
      Need(runtime.ReadMemory(id,ENTRY_STAGE_VA,&stage,4)&&runtime.ReadMemory(id,ENTRY_RESULT_VA,&result,4),"real entry stage/result");
      Need(runtime.MemoryStatistics(id,memory),"stopped resident statistics");
      Need(syscalls.count&&syscalls.consistent&&syscalls.limit==2641,"actual large process self-query reports its real capacity");
      printf((char*)"V8 SELF QUERY");Value(" calls=",syscalls.count);Value(" capacity=",syscalls.limit);printf((char*)"\n");
      printf((char*)"V8 EXECUTION");Value(" stage=",stage);Value(" result=",result);Value(" exit=",status.exitCode);Value(" vector=",status.faultVector);Value(" fault_address=",status.faultAddress);Value(" calls=",status.systemCalls);Value(" pages=",memory.residentPages);Value(" dynamic=",memory.dynamicPages);Value(" peer=",peerProgress);printf((char*)"\n");
      Need(stage>=1&&status.observedCs==0x23&&status.observedCr3==status.directory&&peerProgress,"real CPL3 entry and kernel continuation");
      Need(runtime.Reap()==1&&frames.getStatistics().freeFrames==baseline,"full V8 image/data/metadata/table/FP reap");
      printf((char*)"V8 REAP");Value(" free=",frames.getStatistics().freeFrames);Value(" baseline=",baseline);Value(" checks=",checks);printf((char*)"\n");
    }
    SmallReuse(runtime,tasks,frames,syscalls);
    Finish(true);
}
