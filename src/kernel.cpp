#include <common/types.h>
#include <gdt.h>
#include <memorymanagement.h>
#include <memory/physical.h>
#include <memory/selftest.h>
#include <memory/paging.h>
#include <memory/criticalsection.h>
#include <hardwarecommunication/interrupts.h>
#include <hardwarecommunication/cpu.h>
#include <hardwarecommunication/cpu_startup.h>
#include <hardwarecommunication/cpu_work_pool.h>
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
#include <process/native_runtime.h>
using namespace gtos;
using namespace gtos::hardwarecommunication;
using namespace gtos::drivers;
static bool graphicsActive = false;
static process::NativeRuntime nativeRuntime;
void printf(char *text) {
    memory::InterruptGuard guard; // BSP console messages remain atomic across task switches.
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
// The demonstration is a non-blocking BSP client, not an AP scheduler. Four
// rotating slots are serviced per desktop iteration, with one bounded job per
// worker per second and at most one live ticket for each worker.
struct WorkerDemoSlot {
    CpuWorkTicket ticket;
    uint32_t apicId, seed, nextTick, submittedTick, completions;
    bool pending, active, verified, kicked, disabled, failureReported;
};
static WorkerDemoSlot workerDemo[256];
static uint32_t workerDemoCount = 0, workerDemoCursor = 0;
static uint32_t verifiedWorkerJobs = 0, verifiedWorkerCPUs = 0, workerDemoFailures = 0;
static bool workerRuntimeReported = false, workerPeriodicReported = false;
static uint32_t periodicWorkerCPUs = 0;
static void WorkerEvent(const char *message, uint32_t apicId) {
    // Keep each debug marker intact if a PIT switch would print from another
    // BSP task. This small diagnostic critical section never waits for an AP.
    memory::InterruptGuard guard;
    LogValue(message, apicId);
}
static void WorkerDemoFailure(WorkerDemoSlot &slot, const char *message) {
    slot.disabled = true;
    if (!slot.failureReported) {
        slot.failureReported = true;
        ++workerDemoFailures;
        WorkerEvent(message, slot.apicId);
    }
}
static void InitializeWorkerDemo(CpuWorkPool &workers) {
    workerDemoCount = workers.GetReport().configuredWorkers;
    if (workerDemoCount > 256)
        workerDemoCount = 256;
    for (uint32_t index = 0; index < workerDemoCount; ++index) {
        CpuWorkerSnapshot worker;
        if (workers.GetWorker(index, worker))
            workerDemo[index].apicId = worker.apicId;
    }
}
static void ServiceWorkerDemo(CpuWorkPool &workers, uint32_t now) {
    if (!workerDemoCount)
        return;
    uint32_t visits = workerDemoCount < 4 ? workerDemoCount : 4;
    while (visits--) {
        uint32_t index = workerDemoCursor++;
        if (workerDemoCursor == workerDemoCount)
            workerDemoCursor = 0;
        WorkerDemoSlot &slot = workerDemo[index];
        CpuWorkerSnapshot worker;
        if (!workers.GetWorker(index, worker))
            continue;
        bool ready = worker.state == CpuWorkerIdle || worker.state == CpuWorkerBusy;
        if (ready)
            slot.active = true;
        if (slot.pending) {
            WorkResult result;
            CpuWorkCollectStatus status = workers.Collect(slot.ticket, result);
            if (status == CpuWorkComplete) {
                slot.pending = false;
                slot.nextTick = now + 100;
                if (result.executingApicId != slot.apicId || result.value != slot.seed + 8390656U) {
                    WorkerDemoFailure(slot, "WORKER RUNTIME FAIL RESULT APIC ");
                } else {
                    ++verifiedWorkerJobs;
                    if (++slot.completions == 2)
                        ++periodicWorkerCPUs;
                    if (!slot.verified) {
                        slot.verified = true;
                        ++verifiedWorkerCPUs;
                        WorkerEvent("WORKER JOB VERIFIED APIC ", slot.apicId);
                    }
                }
            } else if (status == CpuWorkPending) {
                uint32_t elapsed = now - slot.submittedTick;
                if (elapsed >= 10 && !slot.kicked) {
                    slot.kicked = true;
                    workers.Kick(slot.apicId); // Same ticket; never duplicate work.
                }
                if (elapsed >= 200)
                    WorkerDemoFailure(slot, "WORKER RUNTIME FAIL STALLED APIC ");
            } else {
                slot.pending = false;
                WorkerDemoFailure(slot, "WORKER RUNTIME FAIL TICKET APIC ");
            }
        }
        if (slot.active && worker.state == CpuWorkerFaulted)
            WorkerDemoFailure(slot, "WORKER RUNTIME FAIL FAULT APIC ");
        if (!ready || slot.pending || slot.disabled || (int32_t)(now - slot.nextTick) < 0)
            continue;
        slot.seed = now ^ (slot.apicId * 0x9E3779B9U);
        CpuWorkSubmitStatus submitted =
            workers.Submit(slot.apicId, WorkRequest(WorkModularSum, 4096, slot.seed), slot.ticket);
        if (submitted == CpuWorkAccepted || submitted == CpuWorkAcceptedWakeFailed) {
            slot.pending = true;
            slot.submittedTick = now;
            slot.kicked = false;
            if (submitted == CpuWorkAcceptedWakeFailed)
                WorkerEvent("WORKER WAKE LIMITED APIC ", slot.apicId);
        } else if (submitted == CpuWorkFull || submitted == CpuWorkOffline) {
            slot.nextTick = now + 10;
        } else {
            WorkerDemoFailure(slot, "WORKER RUNTIME FAIL SUBMIT APIC ");
        }
    }
    CpuWorkPoolReport report = workers.GetReport();
    if (!workerRuntimeReported && report.readyWorkers &&
        verifiedWorkerCPUs >= report.readyWorkers && !workerDemoFailures) {
        workerRuntimeReported = true;
        memory::InterruptGuard guard;
        printf("WORKER RUNTIME PASS\n");
        LogValue("WORKER VERIFIED CPUS ", verifiedWorkerCPUs);
    }
    if (!workerPeriodicReported && report.readyWorkers &&
        periodicWorkerCPUs >= report.readyWorkers && !workerDemoFailures) {
        workerPeriodicReported = true;
        memory::InterruptGuard guard;
        printf("WORKER PERIODIC PASS\n");
        LogValue("WORKER VERIFIED JOBS ", verifiedWorkerJobs);
    }
}
struct NativeProbeData {
    uint32_t signature, progress, errors;
};
static uint32_t nativeFaultId = 0, nativePeerId = 0, nativeBrowserId = 0, nativeFrameBaseline = 0,
                nativeBeginTick = 0;
static bool nativeDemoReady = false, nativeDemoReported = false, nativePeerAfterFault = false;
static bool StartNativeDemo(TaskManager &tasks, GlobalDescriptorTable &gdt,
                            const memory::MultibootInfo *boot) {
    if (!paging.sealForSharedProcessors() || !nativeRuntime.Activate(tasks, gdt, paging, frames))
        return false;
    nativeFrameBaseline = frames.getStatistics().freeFrames;
    if (!(boot->flags & (1u << 3)) || boot->moduleCount < 4)
        return false;
    const memory::MultibootModule *modules = (const memory::MultibootModule *)boot->modules;
    for (uint32_t index = 1; index <= 3; ++index)
        if (modules[index].end <= modules[index].start ||
            modules[index].end - modules[index].start > 65536)
            return false;
    if (!nativeRuntime.CreateElf((const uint8_t *)modules[1].start,
                                 modules[1].end - modules[1].start, nativeFaultId) ||
        !nativeRuntime.CreateElf((const uint8_t *)modules[2].start,
                                 modules[2].end - modules[2].start, nativePeerId) ||
        !nativeRuntime.CreateElf((const uint8_t *)modules[3].start,
                                 modules[3].end - modules[3].start, nativeBrowserId)) {
        if (nativeFaultId)
            nativeRuntime.RequestExit(nativeFaultId, 0xE0);
        if (nativePeerId)
            nativeRuntime.RequestExit(nativePeerId, 0xE0);
        if (nativeBrowserId)
            nativeRuntime.RequestExit(nativeBrowserId, 0xE0);
        nativeRuntime.Reap();
        return false;
    }
    nativeBeginTick = tasks.Ticks();
    printf("NATIVE ELF PROCESSES READY\n");
    return true;
}
static void ServiceNativeDemo(TaskManager &tasks) {
    if (!nativeDemoReady || nativeDemoReported)
        return;
    process::NativeStatus fault, peer, browser;
    if (!nativeRuntime.Status(nativeFaultId, fault) || !nativeRuntime.Status(nativePeerId, peer) ||
        !nativeRuntime.Status(nativeBrowserId, browser))
        return;
    if (!fault.live && fault.faultVector == 14 && peer.live)
        nativePeerAfterFault = true;
    if (fault.live || peer.live || browser.live) {
        if (tasks.Ticks() - nativeBeginTick < 1000)
            return;
        nativeRuntime.RequestExit(nativeFaultId, 0xE6);
        nativeRuntime.RequestExit(nativePeerId, 0xE6);
        nativeRuntime.RequestExit(nativeBrowserId, 0xE6);
        nativeRuntime.Reap();
        nativeDemoReported = true;
        printf("NATIVE RUNTIME FAIL TIMEOUT\n");
        return;
    }
    NativeProbeData a = {}, b = {};
    bool isolated = nativeRuntime.ReadMemory(nativeFaultId, process::NativeRuntime::DataAddress, &a,
                                             sizeof(a)) &&
                    nativeRuntime.ReadMemory(nativePeerId, process::NativeRuntime::DataAddress, &b,
                                             sizeof(b)) &&
                    a.signature == 0xA11CE001U && b.signature == 0xB22CE002U && a.progress &&
                    b.progress && !a.errors && !b.errors;
    bool valid = isolated && nativePeerAfterFault && fault.faultVector == 14 &&
                 fault.faultAddress == 0x100000 && (fault.faultError & 7) == 7 &&
                 peer.faultVector == 0 && peer.exitCode == 0 && (fault.observedCs & 3) == 3 &&
                 (peer.observedCs & 3) == 3 && fault.directory != peer.directory &&
                 fault.observedCr3 == fault.directory && peer.observedCr3 == peer.directory &&
                 fault.directory != paging.getStatistics().directoryAddress &&
                 peer.directory != paging.getStatistics().directoryAddress &&
                 fault.statistics.runTicks && peer.statistics.runTicks;
    valid = valid && browser.exitCode == 0 && browser.faultVector == 0 &&
            (browser.observedCs & 3) == 3 && browser.observedCr3 == browser.directory &&
            browser.directory != fault.directory && browser.directory != peer.directory &&
            browser.directory != paging.getStatistics().directoryAddress;
    uint32_t reaped = nativeRuntime.Reap();
    process::NativeStatistics counts = nativeRuntime.Statistics();
    valid = valid && reaped == 3 && counts.created == 3 && counts.faulted == 1 &&
            counts.exited == 2 && counts.reaped == 3 &&
            frames.getStatistics().freeFrames == nativeFrameBaseline;
    nativeDemoReported = true;
    memory::InterruptGuard guard;
    LogValue("NATIVE FAULT PROCESS CR3 ", fault.observedCr3);
    LogValue("NATIVE PEER PROCESS CR3 ", peer.observedCr3);
    LogValue("NATIVE PEER PROGRESS ", b.progress);
    LogValue("BROWSER PROBE EXIT ", browser.exitCode);
    LogValue("NATIVE REAPED ", reaped);
    printf(valid ? "NATIVE RUNTIME PASS\n" : "NATIVE RUNTIME FAIL ISOLATION OR REAP\n");
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
    TaskManager tasks;
    activeTasks = &tasks;
    bool schedulerOK = TaskManager::RunSelfTests(&gdt);
    printf(schedulerOK ? "SCHEDULER SELFTEST PASS\n" : "SCHEDULER SELFTEST FAIL\n");
    if (!schedulerOK)
        Panic("SCHEDULER SELFTEST");
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    // Prepare resources before identity mappings are built. BSP GDT/IDT already
    // exist, while interrupts and paging remain disabled.
    CpuStartup cpuStartup;
    bool apsPrepared = cpuStartup.Prepare(cpu.GetInfo(), frames, ramMiB * 1024 * 1024);
    CpuWorkPool workers;
    bool workersPrepared = apsPrepared && workers.Prepare(cpuStartup, frames);
    CpuStartupReport apReport = cpuStartup.GetReport();
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
    memory::PagingDeviceRange devices[3] = {{0xA0000, 0x20000}, {0, 0}, {0, 0}};
    uint32_t deviceCount = 1;
    if (modern) {
        uint64_t begin = mbi->framebufferAddress & ~4095ULL;
        uint64_t end =
            (mbi->framebufferAddress + (uint64_t)mode.pitch * mode.height + 4095) & ~4095ULL;
        if (end > 0x100000000ULL || end <= begin || end - begin > 0xFFFFFFFFULL)
            Panic("FRAMEBUFFER MAPPING RANGE");
        devices[deviceCount].address = (uint32_t)begin;
        devices[deviceCount++].length = (uint32_t)(end - begin);
    }
    // Never guess a LAPIC address: Prepare validated this page against the
    // executing BSP's APIC-base MSR, firmware inventory and hardware identity.
    if (apsPrepared && apReport.localApicAddress) {
        devices[deviceCount].address = apReport.localApicAddress;
        devices[deviceCount++].length = 4096;
    }
    const memory::PagingConfig pagingConfig = {(uint32_t)&kernel_start,
                                               (uint32_t)&kernel_end,
                                               (uint32_t)&kernel_readonly_start,
                                               (uint32_t)&kernel_readonly_end,
                                               mbi,
                                               devices,
                                               deviceCount};
    if (!paging.prepareIdentity(frames, pagingConfig))
        Panic(memory::KernelPaging::ErrorName(paging.getLastError()));
    bool nativeStacksPrepared = nativeRuntime.PrepareStacks(paging, frames);
    bool workersStarted = workersPrepared && workers.Start(paging);
    // Only a pre-handoff failure may use the original parked path. Once an AP
    // has been handed off (even if it times out), never resend INIT/SIPI.
    if (!workersStarted && apsPrepared && cpuStartup.ReadyToStart()) {
        printf("WORKER PARKED FALLBACK\n");
        cpuStartup.StartPrepared();
    }
    apReport = cpuStartup.GetReport();
    printf(apReport.error == CpuStartupOk ? "AP STARTUP PASS\n" : "AP STARTUP LIMITED\n");
    printf(CpuStartup::ErrorName(apReport.error));
    printf("\n");
    LogValue("AP INITIALIZED ", apReport.initializedAps);
    LogValue("AP PARKED ", apReport.parkedAps);
    LogValue("AP FAILED ", apReport.failedAps);
    CpuWorkPoolReport workerReport = workers.GetReport();
    printf(workersStarted ? "WORKER POOL READY\n" : "WORKER POOL LIMITED\n");
    if (apsPrepared) {
        printf(CpuWorkPool::ErrorName(workerReport.error));
        printf("\n");
    }
    LogValue("WORKER CONFIGURED ", workerReport.configuredWorkers);
    LogValue("WORKER READY ", workerReport.readyWorkers);
    LogValue("WORKER FAILED ", workerReport.faultedWorkers);
    for (uint32_t index = 0; index < workerReport.configuredWorkers; ++index) {
        CpuWorkerSnapshot worker;
        if (workers.GetWorker(index, worker) && worker.state == CpuWorkerFaulted) {
            LogValue("WORKER LIMITED APIC ", worker.apicId);
            LogValue("WORKER FAILURE REASON ", (uint32_t)worker.failure);
        }
    }
    if (!paging.enable())
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
    nativeDemoReady = nativeStacksPrepared && StartNativeDemo(tasks, gdt, mbi);
    if (!nativeDemoReady)
        printf("NATIVE RUNTIME LIMITED\n");
    // Explicit 100 Hz PIT, so VM/game timing is independent of loop throughput.
    Port8Bit pitControl(0x43), pitData(0x40);
    uint16_t divisor = 1193182 / 100;
    pitControl.Write(0x36);
    pitData.Write(divisor & 255);
    pitData.Write(divisor >> 8);
    interrupts.Activate();
    printf("DESKTOP READY\n");
    bool runtimeChecked = false;
    InitializeWorkerDemo(workers);
    for (;;) {
        ServiceWorkerDemo(workers, tasks.Ticks());
        ServiceNativeDemo(tasks);
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
        workerReport = workers.GetReport();
        snapshot.workerCPUs = workerReport.readyWorkers;
        snapshot.busyWorkers = workerReport.busyWorkers;
        snapshot.workerFailures = workerReport.faultedWorkers;
        snapshot.completedJobs = workerReport.completedJobs;
        snapshot.verifiedJobs = verifiedWorkerJobs;
        snapshot.workerDemoFailures = workerDemoFailures;
        snapshot.workPoolOK = workersStarted && !workerReport.faultedWorkers && !workerDemoFailures;
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
