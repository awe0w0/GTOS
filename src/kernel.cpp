#include <common/types.h>
#include <gdt.h>
#include <memorymanagement.h>
#include <memory/physical.h>
#include <memory/selftest.h>
#include <hardwarecommunication/interrupts.h>
#include <hardwarecommunication/cpu.h>
#include <hardwarecommunication/cpu_startup.h>
#include <hardwarecommunication/port.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <drivers/ata.h>
#include <drivers/vga.h>
#include <gui/shell.h>
#include <multitasking.h>
#include <syscalls.h>
using namespace gtos;
using namespace gtos::hardwarecommunication;
using namespace gtos::drivers;
static bool graphicsActive=false;
void printf(char* text){
    static uint32_t x=0,y=0;volatile uint16_t* video=(volatile uint16_t*)0xB8000;
    for(uint32_t i=0;text&&text[i];++i){char c=text[i];asm volatile("outb %0,$0xe9"::"a"((uint8_t)c));
      if(graphicsActive)continue;if(c=='\n'){++y;x=0;}else{video[y*80+x]=0x0700|(uint8_t)c;++x;}
      if(x>=80){x=0;++y;}if(y>=25){for(uint32_t j=0;j<24*80;++j)video[j]=video[j+80];for(uint32_t j=24*80;j<25*80;++j)video[j]=0x0720;y=24;}}
}
void printf(const char* text){printf((char*)text);}
void printfHex(uint8_t v){char t[3]={"0123456789ABCDEF"[v>>4],"0123456789ABCDEF"[v&15],0};printf(t);}
void printfHex16(uint16_t v){printfHex(v>>8);printfHex(v);}
void printfHex32(uint32_t v){printfHex16(v>>16);printfHex16(v);}
static void LogValue(const char* label,uint32_t v){printf((char*)label);printfHex32(v);printf("\n");}
static void Panic(const char* message){printf("PANIC ");printf((char*)message);printf("\n");for(;;)asm volatile("cli; hlt");}
typedef void (*constructor)();
extern "C" constructor start_ctors,end_ctors;
extern "C" void callConstructors(){for(constructor* i=&start_ctors;i!=&end_ctors;++i)(*i)();}
extern "C" uint8_t kernel_start,kernel_end;
static memory::PhysicalMemoryManager frames;
static TaskManager* activeTasks;
static volatile uint32_t sleeperWakes=0,yielderRuns=0;
static void Sleeper(){for(uint32_t i=0;i<3;++i){if(!activeTasks->SleepCurrent(20))return;++sleeperWakes;}printf("TASK SLEEP RETURN OK\n");}
static void Yielder(){for(uint32_t i=0;i<10;++i){++yielderRuns;if(!activeTasks->YieldCurrent())return;}printf("TASK YIELD RETURN OK\n");asm volatile("int $0x80"::"a"(4),"b"("SYSCALL ABI PASS\n"):"memory","cc");}
extern "C" void kernelMain(void* multiboot,uint32_t magic){
    printf("GTOS 0.2 FOUNDATION BOOT\n");GlobalDescriptorTable gdt;
    if(!frames.initialize(multiboot,magic,(uint32_t)&kernel_start,(uint32_t)&kernel_end))Panic("INVALID MEMORY MAP");
    memory::PhysicalMemoryStatistics physical=frames.getStatistics();
    LogValue("MEMORY FREE FRAMES ",physical.freeFrames);
    bool physicalOK=memory::RunPhysicalMemorySelfTest(frames);
    printf(physicalOK?"PHYSICAL SELFTEST PASS\n":"PHYSICAL SELFTEST FAIL\n");
    uint32_t heapAddress=0;const uint32_t heapPages=1024;
    if(!frames.allocateContiguous(heapPages,heapAddress))Panic("NO HEAP RAM");
    MemoryManager heap(heapAddress,heapPages*4096);
    bool memoryOK=physicalOK&&memory::RunHeapSelfTest()&&heap.validate();
    printf(memoryOK?"HEAP SELFTEST PASS\n":"HEAP SELFTEST FAIL\n");
    if(!memoryOK)Panic("MEMORY SELFTEST");
    const memory::MultibootInfo* mbi=(const memory::MultibootInfo*)multiboot;
    uint32_t ramMiB=(mbi->flags&1)?(mbi->memUpper+2047)/1024:physical.addressableFrames/256;
    CpuManager cpu;cpu.Detect(ramMiB*1024*1024);
    printf("CPU VENDOR ");printf((char*)cpu.GetInfo().vendor);printf(" SOURCE ");printf((char*)cpu.EnumerationSourceName());printf("\n");
    LogValue("CPU DETECTED ",cpu.DetectedLogicalProcessors());LogValue("CPU ONLINE ",cpu.OnlineProcessors());
    CpuStartup cpuStartup;
    bool apsStarted = cpuStartup.Start(cpu.GetInfo(), frames, ramMiB * 1024 * 1024);
    CpuStartupReport apReport = cpuStartup.GetReport();
    printf(apsStarted ? "AP STARTUP PASS\n" : "AP STARTUP LIMITED\n");
    printf((char*)CpuStartup::ErrorName(apReport.error)); printf("\n");
    LogValue("AP PARKED ", apReport.parkedAps);
    LogValue("AP FAILED ", apReport.failedAps);
    TaskManager tasks;activeTasks=&tasks;
    bool schedulerOK=TaskManager::RunSelfTests(&gdt);
    printf(schedulerOK?"SCHEDULER SELFTEST PASS\n":"SCHEDULER SELFTEST FAIL\n");
    if(!schedulerOK)Panic("SCHEDULER SELFTEST");
    InterruptsManager interrupts(0x20,&gdt,&tasks);SyscallHandler syscalls(&interrupts,0x80);
    AdvancedTechnologyAttachment disk(0x1F0,true);storage::AppStore store(&disk);
    bool diskOK=store.Mount();printf(diskOK?"APP STORE MOUNT OK\n":"APP STORE UNAVAILABLE\n");
    LogValue("APP STORE COUNT ",store.Count());LogValue("APP STORE GENERATION ",store.Generation());
    gui::DesktopShell desktop(&store);
    if((mbi->flags&(1<<3))&&mbi->moduleCount){const memory::MultibootModule* m=(const memory::MultibootModule*)mbi->modules;
      if(m[0].end>m[0].start&&m[0].end-m[0].start<=apps::PackageLimit){desktop.SetInstaller((const uint8_t*)m[0].start,m[0].end-m[0].start);printf("APP INSTALLER MODULE READY\n");}}
    KeyboardDriver keyboard(&interrupts,&desktop);MouseDriver mouse(&interrupts,&desktop);
    keyboard.Activate();mouse.Activate();
    VideoGraphicsArray vga;if(!vga.SetMode(320,200,8))Panic("VGA MODE");graphicsActive=true;
    Task sleeper(&gdt,Sleeper),yielder(&gdt,Yielder);tasks.AddTask(&sleeper);tasks.AddTask(&yielder);
    // Explicit 100 Hz PIT, so VM/game timing is independent of loop throughput.
    Port8Bit pitControl(0x43),pitData(0x40);uint16_t divisor=1193182/100;pitControl.Write(0x36);pitData.Write(divisor&255);pitData.Write(divisor>>8);
    interrupts.Activate();printf("DESKTOP READY\n");
    bool runtimeChecked=false;
    for(;;){
      if(!runtimeChecked&&sleeper.State()==TaskTerminated&&yielder.State()==TaskTerminated){runtimeChecked=true;schedulerOK=schedulerOK&&sleeperWakes==3&&yielderRuns==10&&tasks.ContextSwitches()>10;
        printf(schedulerOK?"SCHEDULER RUNTIME PASS\n":"SCHEDULER RUNTIME FAIL\n");tasks.RemoveTask(&sleeper);tasks.RemoveTask(&yielder);}
      gui::SystemSnapshot snapshot;physical=frames.getStatistics();HeapStatistics hs=heap.getStatistics();
      snapshot.ramMiB=ramMiB;snapshot.freePages=physical.freeFrames;snapshot.heapKiB=hs.totalBytes/1024;snapshot.heapUsedKiB=hs.usedBytes/1024;
      snapshot.logicalCPUs=cpu.DetectedLogicalProcessors();snapshot.onlineCPUs=cpu.OnlineProcessors();snapshot.parkedAPs=apReport.parkedAps;snapshot.pagingEnabled=false;snapshot.writeProtectEnabled=false;snapshot.ticks=tasks.Ticks();snapshot.taskCount=tasks.TaskCount()+1;snapshot.contextSwitches=tasks.ContextSwitches();snapshot.diskSectors=disk.SectorCount();
      snapshot.memoryOK=memoryOK&&hs.valid;snapshot.schedulerOK=schedulerOK;snapshot.diskOK=diskOK;for(uint32_t i=0;i<13;++i)snapshot.vendor[i]=cpu.GetInfo().vendor[i];
      desktop.Update(snapshot);asm volatile("sti; hlt");
    }
}
