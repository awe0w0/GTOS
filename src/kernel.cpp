#include <common/types.h>
#include <gdt.h>
#include <memorymanagement.h>
#include <memory/physical.h>
#include <memory/selftest.h>
#include <memory/paging.h>
#include <hardwarecommunication/interrupts.h>
#include <hardwarecommunication/cpu.h>
#include <hardwarecommunication/cpu_startup.h>
#include <hardwarecommunication/port.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <drivers/ata.h>
#include <storage/settings.h>
#include <drivers/vga.h>
#include <drivers/framebuffer.h>
#include <gui/shell.h>
#include <gui/modern_desktop.h>
#include <multitasking.h>
#include <syscalls.h>
using namespace gtos;
using namespace gtos::hardwarecommunication;
using namespace gtos::drivers;
static bool graphicsActive = false;
void printf(char *text) {
    static uint32_t x = 0, y = 0;
    volatile uint16_t *video = (volatile uint16_t *)0xB8000;
    for (uint32_t i = 0; text && text[i]; ++i) {
        char c = text[i];
        asm volatile("outb %0,$0xe9" ::"a"((uint8_t)c));
        if (graphicsActive)
            continue;
        if (c == '\n') {
            ++y;
            x = 0;
        } else {
            video[y * 80 + x] = 0x0700 | (uint8_t)c;
            ++x;
        }
        if (x >= 80) {
            x = 0;
            ++y;
        }
        if (y >= 25) {
            for (uint32_t j = 0; j < 24 * 80; ++j)
                video[j] = video[j + 80];
            for (uint32_t j = 24 * 80; j < 25 * 80; ++j)
                video[j] = 0x0720;
            y = 24;
        }
    }
}
void printf(const char *text) {
    printf((char *)text);
}
void printfHex(uint8_t v) {
    char t[3] = {"0123456789ABCDEF"[v >> 4], "0123456789ABCDEF"[v & 15], 0};
    printf(t);
}
void printfHex16(uint16_t v) {
    printfHex(v >> 8);
    printfHex(v);
}
void printfHex32(uint32_t v) {
    printfHex16(v >> 16);
    printfHex16(v);
}
static void LogValue(const char *label, uint32_t v) {
    printf((char *)label);
    printfHex32(v);
    printf("\n");
}
static void Panic(const char *message) {
    printf("PANIC ");
    printf((char *)message);
    printf("\n");
    for (;;)
        asm volatile("cli; hlt");
}
typedef void (*constructor)();
extern "C" constructor start_ctors, end_ctors;
extern "C" void callConstructors() {
    for (constructor *i = &start_ctors; i != &end_ctors; ++i)
        (*i)();
}
extern "C" uint8_t kernel_start, kernel_end, kernel_readonly_start, kernel_readonly_end;
static memory::PhysicalMemoryManager frames;
static memory::KernelPaging paging;
static TaskManager *activeTasks;
static volatile uint32_t sleeperWakes = 0, yielderRuns = 0;
static void Sleeper() {
    for (uint32_t i = 0; i < 3; ++i) {
        if (!activeTasks->SleepCurrent(20))
            return;
        ++sleeperWakes;
    }
    printf("TASK SLEEP RETURN OK\n");
}
static void Yielder() {
    for (uint32_t i = 0; i < 10; ++i) {
        ++yielderRuns;
        if (!activeTasks->YieldCurrent())
            return;
    }
    printf("TASK YIELD RETURN OK\n");
    asm volatile("int $0x80" ::"a"(4), "b"("SYSCALL ABI PASS\n") : "memory", "cc");
}
static bool BootOption(const memory::MultibootInfo *info, const char *option) {
    if (!(info->flags & 4) || !info->commandLine)
        return false;
    const char *line = (const char *)info->commandLine;
    for (uint32_t i = 0; i < 256 && line[i]; ++i) {
        if (i && line[i - 1] != ' ')
            continue;
        uint32_t n = 0;
        while (i + n < 256 && option[n] && line[i + n] == option[n])
            ++n;
        if (!option[n] && i + n < 256 && (!line[i + n] || line[i + n] == ' '))
            return true;
    }
    return false;
}
extern "C" void kernelMain(void *multiboot, uint32_t magic) {
    printf("GTOS 0.3 PROTECTED DESKTOP BOOT\n");
    GlobalDescriptorTable gdt;
    if (!frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end))
        Panic("INVALID MEMORY MAP");
    memory::PhysicalMemoryStatistics physical = frames.getStatistics();
    LogValue("MEMORY FREE FRAMES ", physical.freeFrames);
    bool physicalOK = memory::RunPhysicalMemorySelfTest(frames);
    printf(physicalOK ? "PHYSICAL SELFTEST PASS\n" : "PHYSICAL SELFTEST FAIL\n");
    uint32_t heapAddress = 0;
    const uint32_t heapPages = 1024;
    if (!frames.allocateContiguous(heapPages, heapAddress))
        Panic("NO HEAP RAM");
    MemoryManager heap(heapAddress, heapPages * 4096);
    bool memoryOK = physicalOK && memory::RunHeapSelfTest() && heap.validate();
    printf(memoryOK ? "HEAP SELFTEST PASS\n" : "HEAP SELFTEST FAIL\n");
    if (!memoryOK)
        Panic("MEMORY SELFTEST");
    const memory::MultibootInfo *mbi = (const memory::MultibootInfo *)multiboot;
    uint32_t ramMiB =
        (mbi->flags & 1) ? (mbi->memUpper + 2047) / 1024 : physical.addressableFrames / 256;
    CpuManager cpu;
    cpu.Detect(ramMiB * 1024 * 1024);
    printf("CPU VENDOR ");
    printf((char *)cpu.GetInfo().vendor);
    printf(" SOURCE ");
    printf((char *)cpu.EnumerationSourceName());
    printf("\n");
    LogValue("CPU DETECTED ", cpu.DetectedLogicalProcessors());
    LogValue("CPU ONLINE ", cpu.OnlineProcessors());
    CpuStartup cpuStartup;
    bool apsStarted = cpuStartup.Start(cpu.GetInfo(), frames, ramMiB * 1024 * 1024);
    CpuStartupReport apReport = cpuStartup.GetReport();
    printf(apsStarted ? "AP STARTUP PASS\n" : "AP STARTUP LIMITED\n");
    printf((char *)CpuStartup::ErrorName(apReport.error));
    printf("\n");
    LogValue("AP PARKED ", apReport.parkedAps);
    LogValue("AP FAILED ", apReport.failedAps);
    TaskManager tasks;
    activeTasks = &tasks;
    bool schedulerOK = TaskManager::RunSelfTests(&gdt);
    printf(schedulerOK ? "SCHEDULER SELFTEST PASS\n" : "SCHEDULER SELFTEST FAIL\n");
    if (!schedulerOK)
        Panic("SCHEDULER SELFTEST");
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    AdvancedTechnologyAttachment disk(0x1F0, true);
    storage::AppStore store(&disk);
    bool diskOK = store.Mount();
    printf(diskOK ? "APP STORE MOUNT OK\n" : "APP STORE UNAVAILABLE\n");
    LogValue("APP STORE COUNT ", store.Count());
    LogValue("APP STORE GENERATION ", store.Generation());
    storage::SettingsStore settings(&disk);
    bool settingsWritable = settings.Load();
    printf(settingsWritable ? "SETTINGS READY\n" : "SETTINGS SESSION ONLY\n");
    LogValue("SETTINGS LOCALE ", (uint32_t)settings.Current().locale);
    LogValue("SETTINGS THEME ", (uint32_t)settings.Current().theme);
    gui::DesktopShell desktop(&store);
    LogValue("MB FLAGS ", mbi->flags);
    LogValue("FB ADDRESS ", (uint32_t)mbi->framebufferAddress);
    LogValue("FB PITCH ", mbi->framebufferPitch);
    LogValue("FB WIDTH ", mbi->framebufferWidth);
    LogValue("FB HEIGHT ", mbi->framebufferHeight);
    LogValue("FB FORMAT ", mbi->framebufferBitsPerPixel | ((uint32_t)mbi->framebufferType << 8));
    printf("FB COLOR INFO ");
    for (uint32_t i = 0; i < 6; ++i)
        printfHex(mbi->framebufferColorInfo[i]);
    printf("\n");
    Framebuffer framebuffer;
    FramebufferMode mode;
    gui::ModernDesktop *modern = 0;
    uint32_t backbuffer = 0, backbufferPages = 0;
    if (!BootOption(mbi, "legacy") && Framebuffer::ReadMode(mbi, mode) && mode.width >= 640 &&
        mode.height >= 480) {
        backbufferPages = (mode.width * mode.height * 4 + 4095) / 4096;
        if (frames.allocateContiguous(backbufferPages, backbuffer) &&
            framebuffer.Configure(mbi, (uint32_t *)backbuffer, backbufferPages * 1024)) {
            printf("FB CONFIGURED\n");
            modern = new gui::ModernDesktop(&framebuffer, &store, &settings);
        }
        if (!modern && backbuffer)
            frames.freeContiguous(backbuffer, backbufferPages);
    }
    printf(modern ? "DESKTOP MODE FRAMEBUFFER\n" : "DESKTOP MODE LEGACY\n");
    if ((mbi->flags & (1 << 3)) && mbi->moduleCount) {
        const memory::MultibootModule *m = (const memory::MultibootModule *)mbi->modules;
        if (m[0].end > m[0].start && m[0].end - m[0].start <= apps::PackageLimit) {
            desktop.SetInstaller((const uint8_t *)m[0].start, m[0].end - m[0].start);
            if (modern)
                modern->SetInstaller((const uint8_t *)m[0].start, m[0].end - m[0].start);
            printf("APP INSTALLER MODULE READY\n");
        }
    }
    KeyboardEventHandler *keyEvents = modern ? (KeyboardEventHandler *)modern : &desktop;
    MouseEventHandler *mouseEvents = modern ? (MouseEventHandler *)modern : &desktop;
    KeyboardDriver keyboard(&interrupts, keyEvents);
    MouseDriver mouse(&interrupts, mouseEvents);
    keyboard.Activate();
    mouse.Activate();
    VideoGraphicsArray vga;
    if (!modern && !vga.SetMode(320, 200, 8))
        Panic("VGA MODE");
    graphicsActive = true;
    memory::PagingDeviceRange devices[2] = {{0xA0000, 0x20000}, {0, 0}};
    uint32_t deviceCount = 1;
    if (modern) {
        uint64_t begin = mbi->framebufferAddress & ~4095ULL;
        uint64_t end =
            (mbi->framebufferAddress + (uint64_t)mode.pitch * mode.height + 4095) & ~4095ULL;
        if (end > 0x100000000ULL || end <= begin || end - begin > 0xFFFFFFFFULL)
            Panic("FRAMEBUFFER MAPPING RANGE");
        devices[1].address = (uint32_t)begin;
        devices[1].length = (uint32_t)(end - begin);
        deviceCount = 2;
    }
    const memory::PagingConfig pagingConfig = {(uint32_t)&kernel_start,
                                               (uint32_t)&kernel_end,
                                               (uint32_t)&kernel_readonly_start,
                                               (uint32_t)&kernel_readonly_end,
                                               mbi,
                                               devices,
                                               deviceCount};
    if (!paging.prepareIdentity(frames, pagingConfig) || !paging.enable())
        Panic(memory::KernelPaging::ErrorName(paging.getLastError()));
    uint32_t cr0;
    asm volatile("mov %%cr0,%0" : "=r"(cr0));
    memory::PagingMapping mapping;
    bool pagingOK = (cr0 & 0x80010000U) == 0x80010000U && !paging.query(0, mapping) &&
                    paging.query((uint32_t)&kernel_readonly_start, mapping) && !mapping.writable &&
                    paging.query(heapAddress, mapping) && mapping.writable;
    if (!pagingOK)
        Panic("PAGING PROTECTION SELFTEST");
    printf("PAGING PG WP NULL RO PASS\n");
    LogValue("PAGING CR3 ", paging.getStatistics().directoryAddress);
    Task sleeper(&gdt, Sleeper), yielder(&gdt, Yielder);
    tasks.AddTask(&sleeper);
    tasks.AddTask(&yielder);
    // Explicit 100 Hz PIT, so VM/game timing is independent of loop throughput.
    Port8Bit pitControl(0x43), pitData(0x40);
    uint16_t divisor = 1193182 / 100;
    pitControl.Write(0x36);
    pitData.Write(divisor & 255);
    pitData.Write(divisor >> 8);
    interrupts.Activate();
    printf("DESKTOP READY\n");
    bool runtimeChecked = false;
    for (;;) {
        if (!runtimeChecked && sleeper.State() == TaskTerminated &&
            yielder.State() == TaskTerminated) {
            runtimeChecked = true;
            schedulerOK = schedulerOK && sleeperWakes == 3 && yielderRuns == 10 &&
                          tasks.ContextSwitches() > 10;
            printf(schedulerOK ? "SCHEDULER RUNTIME PASS\n" : "SCHEDULER RUNTIME FAIL\n");
            tasks.RemoveTask(&sleeper);
            tasks.RemoveTask(&yielder);
        }
        gui::SystemSnapshot snapshot;
        physical = frames.getStatistics();
        HeapStatistics hs = heap.getStatistics();
        snapshot.ramMiB = ramMiB;
        snapshot.freePages = physical.freeFrames;
        snapshot.heapKiB = hs.totalBytes / 1024;
        snapshot.heapUsedKiB = hs.usedBytes / 1024;
        snapshot.logicalCPUs = cpu.DetectedLogicalProcessors();
        snapshot.onlineCPUs = cpu.OnlineProcessors();
        snapshot.parkedAPs = apReport.parkedAps;
        snapshot.pagingEnabled = paging.getStatistics().enabled;
        snapshot.writeProtectEnabled = (cr0 & 0x10000U) != 0;
        snapshot.ticks = tasks.Ticks();
        snapshot.taskCount = tasks.TaskCount() + 1;
        snapshot.contextSwitches = tasks.ContextSwitches();
        snapshot.diskSectors = disk.SectorCount();
        snapshot.memoryOK = memoryOK && hs.valid;
        snapshot.schedulerOK = schedulerOK;
        snapshot.diskOK = diskOK;
        for (uint32_t i = 0; i < 13; ++i)
            snapshot.vendor[i] = cpu.GetInfo().vendor[i];
        if (modern)
            modern->Update(snapshot);
        else
            desktop.Update(snapshot);
        asm volatile("sti; hlt");
    }
}
