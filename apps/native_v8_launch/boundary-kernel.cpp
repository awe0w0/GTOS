#include <memory/paging.h>
// Private corruption fixture only: product headers and objects retain access
// control. Expose this class after its dependency headers have been included.
#define private public
#include <memory/process_address_space.h>
#undef private
#include <process/native_runtime.h>
#include <process/info_abi.h>
#include <process/clock_abi.h>
#include <memory/criticalsection.h>
#include <hardwarecommunication/interrupts.h>
#include <syscalls.h>
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
    uint8_t image[12288];
    uint32_t held[16384],heldCount,checks;
    volatile uint32_t peerProgress;
    const uint32_t StackPages=128,StackBegin=NativeRuntime::UserStackTop-StackPages*4096;
    void Peer() { for (;;) ++peerProgress; }
    [[noreturn]] void Finish(bool pass) {
        printf(pass?(char*)"GTOS LAUNCH BOUNDARIES PASS\n":(char*)"GTOS LAUNCH BOUNDARIES FAIL\n");
        asm volatile("outb %0,$0xf4"::"a"((uint8_t)(pass?0x10:0x11)));for (;;) asm volatile("cli; hlt");
    }
    void Need(bool pass,const char* why) { ++checks;if (!pass) { printf((char*)"FAILED ");printf((char*)why);printf((char*)"\n");Finish(false); } }
    void Value(const char* name,uint32_t value) { printf((char*)name);printfHex32(value); }
    void Put16(uint32_t at,uint32_t value) { image[at]=value;image[at+1]=value>>8; }
    void Put32(uint32_t at,uint32_t value) { for (uint32_t i=0;i<4;i++) image[at+i]=value>>(8*i); }
    void MakeImage(uint32_t mode) {
        for (uint32_t i=0;i<sizeof(image);i++) image[i]=0;
        image[0]=0x7f;image[1]='E';image[2]='L';image[3]='F';image[4]=image[5]=image[6]=1;
        Put16(16,2);Put16(18,3);Put32(20,1);Put32(24,0x40000000);Put32(28,52);
        Put16(40,52);Put16(42,32);Put16(44,2);
        Put32(52,1);Put32(56,4096);Put32(60,0x40000000);Put32(68,64);Put32(72,64);Put32(76,5);Put32(80,4096);
        Put32(84,1);Put32(88,8192);Put32(92,0x40002000);Put32(100,132);Put32(104,4096);Put32(108,6);Put32(112,4096);
        const uint8_t code[]={0xb8,0x13,0x47,0,0,0xbb,0,0x20,0,0x40,0xb9,16,0,0,0,0xcd,0x80};
        for (uint32_t i=0;i<sizeof(code);i++) image[4096+i]=code[i];
        uint32_t at=4096+sizeof(code);
        if (mode==1||mode==2) {
            image[at++]=0xc7;image[at++]=0x05;Put32(at,mode==1?StackBegin-4:NativeRuntime::UserStackTop);at+=4;Put32(at,0x12345678);at+=4;
        } else if (mode==3) { image[at++]=0xeb;image[at++]=0xfe; }
        image[at++]=0x89;image[at++]=0xc3;image[at++]=0xb8;Put32(at,GTOS_SYS_EXIT);at+=4;image[at++]=0xcd;image[at++]=0x80;image[at++]=0x0f;image[at++]=0x0b;
        Put32(8192,1);Put32(8200,0x40002040);Put32(8204,sizeof(GtosProcessInfoResult));
    }
    void HoldAll(PhysicalMemoryManager& frames) {
        heldCount=0;uint32_t address;
        while (frames.allocate(address)) { Need(heldCount<16384,"bounded frame inventory");held[heldCount++]=address; }
    }
    void ReleaseRun(PhysicalMemoryManager& frames,uint32_t count) {
        for (uint32_t start=0;start+count<=heldCount;start++) {
            bool adjacent=true;for (uint32_t i=1;i<count;i++) if (held[start+i]!=held[start]+i*4096) adjacent=false;
            if (!adjacent) continue;
            for (uint32_t i=0;i<count;i++) { Need(frames.free(held[start+i]),"controlled frame release");held[start+i]=0; }
            return;
        }
        Need(false,"controlled contiguous free run");
    }
    void RestoreHeld(PhysicalMemoryManager& frames) {
        for (uint32_t i=0;i<heldCount;i++) if (held[i]) Need(frames.free(held[i]),"restore held frame");
        heldCount=0;
    }
    void DynamicCases(KernelPaging& paging,PhysicalMemoryManager& frames) {
        const uint32_t baseline=frames.getStatistics().freeFrames,end=StackBegin-4096;
        ProcessAddressSpace space;
        const uint32_t invalidEnds[]={0U,ProcessAddressSpace::DynamicBase,ProcessAddressSpace::DynamicLimit+4096,ProcessAddressSpace::DynamicLimit-1};
        for (uint32_t bad : invalidEnds)
            Need(!space.Prepare(paging,frames,512,bad)&&space.GetLastError()==ProcessMemoryBadRange
                &&!space.Prepared()&&frames.getStatistics().freeFrames==baseline,"invalid dynamic end has no allocation");
        Need(space.Prepare(paging,frames,512,end)&&space.Seal(),"bounded dynamic arena prepared");
        uint32_t base=17,handle=19;bool badGuard,badSpan,badLength,accepted,committed;
        const uint32_t kernelDirectory=paging.getStatistics().directoryAddress,own=space.DirectoryAddress();
        asm volatile("mov %0,%%cr3"::"r"(own):"memory");
        badGuard=!space.Reserve(4096,4096,end,base,handle)&&space.GetLastError()==ProcessMemoryBadRange&&base==17&&handle==19;
        badSpan=!space.Reserve(8192,4096,end-4096,base,handle)&&space.GetLastError()==ProcessMemoryBadRange&&base==17&&handle==19;
        badLength=!space.Reserve(end-ProcessAddressSpace::DynamicBase+4096,4096,0,base,handle)&&space.GetLastError()==ProcessMemoryBadRange;
        accepted=space.Reserve(4096,4096,end-4096,base,handle)&&base==end-4096;
        committed=accepted&&space.SetPermissions(handle,0,4096,ProcessMemoryReadWrite);
        asm volatile("mov %0,%%cr3"::"r"(kernelDirectory):"memory");
        Need(badGuard&&badSpan&&badLength&&accepted&&committed,"guard excluded and exact last dynamic page committed");
        uint32_t zero=17;Need(space.CopyFromUser(&zero,end-4096,4)&&zero==0&&space.Destroy(),"zero data and large-limit destruction");
        Need(space.Prepare(paging,frames)&&space.Seal(),"same address-space object default reuse");
        const uint32_t reused=space.DirectoryAddress();asm volatile("mov %0,%%cr3"::"r"(reused):"memory");
        accepted=space.Reserve(4096,4096,ProcessAddressSpace::DynamicLimit-4096,base,handle);
        asm volatile("mov %0,%%cr3"::"r"(kernelDirectory):"memory");
        Need(accepted&&base==ProcessAddressSpace::DynamicLimit-4096&&space.Destroy()
            &&frames.getStatistics().freeFrames==baseline,"default dynamic limit restored on reuse");
        printf((char*)"LAUNCH DYNAMIC PASS\n");
    }
    void IndexCases(KernelPaging& paging,PhysicalMemoryManager& frames) {
        const uint32_t baseline=frames.getStatistics().freeFrames;
        ProcessAddressSpace space;Need(space.Prepare(paging,frames,512),"indexed space prepare");
        const uint32_t addresses[]={0x40000000,0x42000000,0x44000000};
        for (uint32_t address : addresses) Need(space.MapNewPage(address,true),"distinct VA hash collision accepted");
        ProcessMemoryStatistics info={};Need(space.QueryStatistics(info)&&info.residentPages==3&&info.tableFrames==3,"collision records and tables retained");
        uint32_t value=0x12345678,copy=0;
        for (uint32_t address : addresses) Need(space.CopyToUser(address,&value,4)&&space.CopyFromUser(&copy,address,4)&&copy==value,"colliding keys preserve exact lookup");
        const auto saved=space.pages[1];
        space.pages[1].address=space.pages[0].address;
        Need(!space.QueryStatistics(info)&&space.GetLastError()==ProcessMemoryBadMapping,"duplicate VA rejected by full validation");space.pages[1]=saved;
        space.pages[1].frame=space.pages[0].frame;
        Need(!space.QueryStatistics(info)&&space.GetLastError()==ProcessMemoryBadMapping,"duplicate physical ownership rejected");space.pages[1]=saved;
        uint32_t* table=(uint32_t*)space.tables[addresses[0]>>22];
        const uint32_t entry=table[0];table[1]=entry;
        Need(!space.Seal()&&space.GetLastError()==ProcessMemoryBadMapping,"PTE lacking authoritative record rejected");table[1]=0;
        table[0]=entry^2;
        Need(!space.QueryStatistics(info)&&space.GetLastError()==ProcessMemoryBadMapping,"record and PTE permissions must agree");table[0]=entry;
        Need(space.Seal()&&space.Destroy()&&frames.getStatistics().freeFrames==baseline,"restored metadata validates and exact teardown");
        printf((char*)"LAUNCH INDEX CORRUPTION PASS\n");
    }
    void Rejections(NativeRuntime& runtime,PhysicalMemoryManager& frames) {
        const uint32_t baseline=frames.getStatistics().freeFrames;uint32_t id=17;
        const NativeElfLaunchProfile bad[]={ {0,128,1024},{4097,128,1024},{4096,0,1024},{4096,1,1024},
            {4096,129,1024},{4096,~0U,1024},{4096,128,0},{4096,128,1025},{4096,128,~0U},{4096,2,1},{1,128,1024} };
        MakeImage(0);
        for (const NativeElfLaunchProfile& profile : bad)
            Need(!runtime.CreateElf(image,sizeof(image),id,profile)&&id==0&&frames.getStatistics().freeFrames==baseline,"profile bound rejected without side effects");
        const NativeElfLaunchProfile profile={4096,128,1024};
        const uint32_t overlap[]={StackBegin-4096,StackBegin,NativeRuntime::UserStackBottom,NativeRuntime::UserStackTop};
        for (uint32_t address : overlap) { Put32(92,address);Need(!runtime.CreateElf(image,sizeof(image),id,profile)
            &&id==0&&frames.getStatistics().freeFrames==baseline,"ELF cannot cover expanded root stack or either guard"); }
        MakeImage(0);Put32(92,0x41000000);Put32(104,4095*4096);
        Need(!runtime.CreateElf(image,sizeof(image),id,profile)&&id==0&&frames.getStatistics().freeFrames==baseline,"image+stack+runtime total bounded before allocation");
        MakeImage(0);HoldAll(frames);ReleaseRun(frames,10);
        Need(!runtime.CreateElf(image,sizeof(image),id,profile)&&id==0&&frames.getStatistics().freeFrames==10,"root stack physical OOM rolls back image metadata and tables");
        RestoreHeld(frames);Need(frames.getStatistics().freeFrames==baseline,"OOM held inventory restored");
        printf((char*)"LAUNCH REJECTION OOM PASS\n");
    }
}
extern "C" void NativeProcessSmoke(void* multiboot,uint32_t magic) {
    printf((char*)"GTOS LAUNCH BOUNDARIES BOOT\n");
    GlobalDescriptorTable gdt;TaskManager tasks;InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    PhysicalMemoryManager frames;Need(frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end),"physical map");
    KernelPaging paging;PagingConfig config={(uint32_t)&kernel_start,(uint32_t)&kernel_end,(uint32_t)&kernel_readonly_start,(uint32_t)&kernel_readonly_end,(const MultibootInfo*)multiboot,0,0};
    Need(paging.prepareIdentity(frames,config),"kernel template");NativeRuntime runtime;
    Need(runtime.PrepareStacks(paging,frames)&&paging.enable()&&paging.sealForSharedProcessors(),"real shared kernel mappings");
    DynamicCases(paging,frames);IndexCases(paging,frames);Need(runtime.Activate(tasks,gdt,paging,frames,NativeFpSse2),"native activation");Rejections(runtime,frames);
    const uint32_t baseline=frames.getStatistics().freeFrames;Task peer(&gdt,Peer);Need(tasks.AddTask(&peer),"kernel peer");
    asm volatile("outb %0,$0x43"::"a"((uint8_t)0x36));asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR&255)));asm volatile("outb %0,$0x40"::"a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR>>8)));
    interrupts.Activate();
    for (uint32_t mode=0;mode<5;mode++) {
        MakeImage(mode==4?0:mode);uint32_t id=0;const NativeElfLaunchProfile profile={4096,128,1024};
        Need(mode==4?runtime.CreateElf(image,sizeof(image),id):runtime.CreateElf(image,sizeof(image),id,profile),"real process admission");
        NativeStatus status={};const uint32_t start=tasks.BootTicks();
        for (;;) {
            { InterruptGuard guard;Need(runtime.Status(id,status),"actual status");if (!status.live) break;
              Need(tasks.BootTicks()-start<1000,"bounded consumer execution");
              if (mode==3) { GtosProcessInfoResult info={};Need(runtime.ReadMemory(id,0x40002040,&info,sizeof(info)),"cancel observer");
                  if (info.version==1) Need(runtime.RequestExit(id,0x74),"cancel with expanded root stack"); } }
            asm volatile("sti; hlt":::"memory");
        }
        InterruptGuard guard;GtosProcessInfoResult info={};ProcessMemoryStatistics memory={};uint32_t probe=17;
        Need(runtime.ReadMemory(id,0x40002040,&info,sizeof(info))&&runtime.MemoryStatistics(id,memory),"actual self-query and ownership");
        const uint32_t pages=mode==4?2:128,begin=NativeRuntime::UserStackTop-pages*4096,capacity=mode==4?256:1154;
        Need(info.version==1&&info.stack_begin==begin&&info.stack_end==NativeRuntime::UserStackTop
            &&info.maximum_pages==capacity&&memory.capacityPages==capacity&&memory.residentPages==2+pages,"truthful profile/default stack and page bounds");
        Need(runtime.ReadMemory(id,begin,&probe,4)&&probe==0&&!runtime.ReadMemory(id,begin-4,&probe,4)
            &&!runtime.ReadMemory(id,NativeRuntime::UserStackTop,&probe,4),"stack zero and guards unmapped");
        Need(status.observedCs==0x23&&status.observedCr3==status.directory&&peerProgress,"real user mode and peer");
        Need(status.exitCode==(mode==1||mode==2?0x8000000eU:mode==3?0x74U:0U),"normal/guard fault/cancel result");
        if (mode==1||mode==2) Need(status.faultVector==14&&status.faultError==6
            &&status.faultAddress==(mode==1?begin-4:NativeRuntime::UserStackTop),"exact low/high guard hardware fault");
        Need(runtime.Reap()==1&&frames.getStatistics().freeFrames==baseline,"exact root image metadata stack table reap");
        printf((char*)"LAUNCH CASE");Value(" mode=",mode);Value(" capacity=",capacity);Value(" stack=",pages);
        Value(" free=",frames.getStatistics().freeFrames);Value(" baseline=",baseline);printf((char*)"\n");
    }
    Value("LAUNCH CHECKS=",checks);printf((char*)"\n");Finish(true);
}
