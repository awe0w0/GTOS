#include <common/types.h>
#include <common/boot_log.h>
#include <gui/modern_painter.h>
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
#include <storage/liveblockdevice.h>
#include <drivers/vga.h>
#include <drivers/framebuffer.h>
#include <gui/shell.h>
#include <gui/modern_desktop.h>
#include <multitasking.h>
#include <syscalls.h>
#include <process/native_runtime.h>
#include <process/native_surface.h>
using namespace gtos;
using namespace gtos::hardwarecommunication;
using namespace gtos::drivers;
static bool graphicsActive = true; // No assumed VGA text access before boot metadata is validated.
static process::NativeRuntime nativeRuntime;
// Early diagnostics bind only a validated 32-bit RGB GOP range. They never
// allocate or depend on the desktop backbuffer, interrupt dispatcher or heap.
namespace {
FramebufferMode bootScreenMode;
volatile uint8_t* bootScreenVideo = 0;
uint32_t bootColumn = 0, bootRow = 0;
bool bootScreenReady = false, bootScreenEnabled = false, bootRendering = false;
bool bootEmergencyShown = false, serialEnabled = false, panicInProgress = false;
const char* bootPhase = "B01";
uint32_t BootColor(uint32_t color) {
    return ((((color >> 16) & 255) >> (8 - bootScreenMode.redSize)) << bootScreenMode.redPosition) |
           ((((color >> 8) & 255) >> (8 - bootScreenMode.greenSize)) << bootScreenMode.greenPosition) |
           (((color & 255) >> (8 - bootScreenMode.blueSize)) << bootScreenMode.bluePosition);
}
void BootPixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x < bootScreenMode.width && y < bootScreenMode.height)
        ((volatile uint32_t*)(bootScreenVideo + y * bootScreenMode.pitch))[x] = color;
}
void BootClear() {
    uint32_t background = BootColor(0x10202E);
    for (uint32_t y = 0; y < bootScreenMode.height; ++y)
        for (uint32_t x = 0; x < bootScreenMode.width; ++x)
            BootPixel(x, y, background);
    bootColumn = bootRow = 0;
}
void BootScroll() {
    const uint32_t step = 10;
    if ((bootRow + 1) * step <= bootScreenMode.height) return;
    for (uint32_t y = 0; y + step < bootScreenMode.height; ++y) {
        volatile uint32_t* to = (volatile uint32_t*)(bootScreenVideo + y * bootScreenMode.pitch);
        volatile uint32_t* from = (volatile uint32_t*)(bootScreenVideo + (y + step) * bootScreenMode.pitch);
        for (uint32_t x = 0; x < bootScreenMode.width; ++x) to[x] = from[x];
    }
    uint32_t background = BootColor(0x10202E);
    for (uint32_t y = bootScreenMode.height - step; y < bootScreenMode.height; ++y)
        for (uint32_t x = 0; x < bootScreenMode.width; ++x) BootPixel(x, y, background);
    bootRow = bootScreenMode.height / step - 1;
}
void BootCharacter(char character) {
    if (character == '\r') return;
    if (character == '\n') {
        bootColumn = 0; ++bootRow; BootScroll(); return;
    }
    if ((bootColumn + 1) * 6 > bootScreenMode.width) {
        bootColumn = 0; ++bootRow; BootScroll();
    }
    uint32_t foreground = BootColor(bootEmergencyShown ? 0xFFD7B8 : 0xEBF7FF);
    uint32_t background = BootColor(0x10202E);
    const uint8_t* glyph = gui::ModernGameGlyph(character);
    if (!glyph && character != ' ') glyph = gui::ModernGameGlyph('?');
    for (uint32_t y = 0; y < 10; ++y)
        for (uint32_t x = 0; x < 6; ++x)
            BootPixel(bootColumn * 6 + x, bootRow * 10 + y,
                      glyph && y < 7 && x < 5 && (glyph[y] & (1 << (4 - x)))
                          ? foreground : background);
    ++bootColumn;
}
void BootText(const char* text) {
    for (uint32_t i = 0; text && text[i]; ++i) BootCharacter(text[i]);
}
void BootTail(uint32_t rows) {
    const uint32_t lines = common::BootLog::Lines();
    const uint32_t first = lines > rows ? lines - rows : 0;
    char line[321]; // Largest supported width is 1920 / 6 columns.
    uint32_t capacity = bootScreenMode.width / 6 + 1;
    for (uint32_t i = first; i < lines; ++i) {
        if (common::BootLog::ReadLine(i, line, capacity)) {
            BootText(line); BootCharacter('\n');
        }
    }
}
void BootScreenPut(char character) {
    // If writing the diagnostic device faults, the fatal observer must not
    // recurse into that same device. The ring and optional UART remain separate.
    if (!bootScreenReady || !bootScreenEnabled || bootRendering) return;
    bootRendering = true;
    BootCharacter(character);
    bootRendering = false;
}
void RegisterBootScreen(const memory::MultibootInfo* info, bool visible) {
    if (!Framebuffer::ReadMode(info, bootScreenMode) || bootScreenMode.width < 640 ||
        bootScreenMode.height < 480) return;
    bootScreenVideo = (volatile uint8_t*)(uint32_t)info->framebufferAddress;
    bootScreenReady = true;
    if (visible) {
        bootRendering = true;
        bootScreenEnabled = true;
        BootClear();
        BootText("GTOS KERNEL BOOT LOG - RAM ONLY\n");
        BootTail(bootScreenMode.height / 10 - 2);
        bootRendering = false;
    }
}
uint8_t SerialRead(uint16_t port) {
    uint8_t value; asm volatile("inb %1,%0" : "=a"(value) : "Nd"(port)); return value;
}
void SerialWrite(uint16_t port, uint8_t value) {
    asm volatile("outb %0,%1" :: "a"(value), "Nd"(port));
}
bool StartBootSerial() {
    // Only an explicit serial token performs any UART IO. 16550 scratch proves
    // the configured legacy COM1 port before changing its baud/interrupt state.
    if (SerialRead(0x3FD) == 0xFF) return false;
    uint8_t scratch = SerialRead(0x3FF);
    SerialWrite(0x3FF, 0x5A);
    bool present = SerialRead(0x3FF) == 0x5A;
    SerialWrite(0x3FF, scratch);
    if (!present) return false;
    SerialWrite(0x3F9, 0); // UART interrupts stay disabled.
    SerialWrite(0x3FB, 0x80);
    SerialWrite(0x3F8, 1); SerialWrite(0x3F9, 0); // 115200 baud divisor.
    SerialWrite(0x3FB, 3); // 8 data bits, no parity, one stop bit.
    SerialWrite(0x3FA, 0xC7);
    SerialWrite(0x3FC, 3); // DTR/RTS; no IRQ enable.
    return true;
}
void BootSerialByte(char character) {
    // An absent/stalled receiver never causes an infinite boot or panic wait.
    for (uint32_t polls = 0; polls < 4096; ++polls) {
        if (SerialRead(0x3FD) & 0x20) {
            SerialWrite(0x3F8, (uint8_t)character); return;
        }
        asm volatile("pause");
    }
}
void BootSerialPut(char character) {
    if (!serialEnabled) return;
    if (character == '\n') BootSerialByte('\r');
    BootSerialByte(character);
}
void ReplayBootSerial() {
    char line[321];
    uint32_t count = common::BootLog::Lines();
    for (uint32_t i = 0; i < count; ++i) {
        if (!common::BootLog::ReadLine(i, line, sizeof(line))) continue;
        for (uint32_t j = 0; line[j]; ++j) BootSerialPut(line[j]);
        BootSerialPut('\n');
    }
}
// The diagnostic menu keeps the real GOP log visible until a real PS/2 key.
// That key is consumed; it cannot accidentally launch or edit an application.
class BootDiagnosticKeyboard : public KeyboardEventHandler {
    KeyboardEventHandler* target;
    volatile bool waiting;
    bool consumeRelease;
    char resumeKey;
  public:
    BootDiagnosticKeyboard(KeyboardEventHandler* next, bool hold)
        : target(next), waiting(hold), consumeRelease(false), resumeKey(0) {}
    bool Waiting() const { return waiting; }
    void OnKeyDown(char key) {
        if (waiting) {
            resumeKey = key; consumeRelease = true; waiting = false; return;
        }
        if (consumeRelease && key == resumeKey) return; // Typematic repeats.
        if (target) target->OnKeyDown(key);
    }
    void OnKeyUp(char key) {
        if (consumeRelease && key == resumeKey) { consumeRelease = false; return; }
        if (!waiting && target) target->OnKeyUp(key);
    }
};
}
// Weakly observed by the existing fatal exception path. This observer neither
// parses logged text nor changes the handled native-fault scheduling contract.
extern "C" void bootLogEmergencyScreen() {
    asm volatile("cli" ::: "memory");
    if (!bootScreenReady || bootRendering || bootEmergencyShown) return;
    bootRendering = true;
    bootEmergencyShown = bootScreenEnabled = true;
    BootClear();
    BootText("GTOS KERNEL FAILURE - PHOTO THIS SCREEN\n");
    BootText("LAST PHASE "); BootText(bootPhase); BootCharacter('\n');
    BootText("RAM LOG LOST ON RESET - NO DISK LOG WRITE\n");
    BootTail(bootScreenMode.height / 10 - 4);
    bootRendering = false;
}

void printf(char *text) {
    memory::InterruptGuard guard; // BSP console messages remain atomic across task switches.
    static uint32_t x = 0, y = 0;
    volatile uint16_t *video = (volatile uint16_t *)0xB8000;
    for (uint32_t i = 0; text && text[i]; ++i) {
        char c = text[i];
        common::BootLog::Put(c);
        // E9 is an emulator debug sink, not a physical-machine log device.
        asm volatile("outb %0,$0xe9" ::"a"((uint8_t)c));
        BootSerialPut(c);
        BootScreenPut(c);
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
static void BootStage(const char* phase, const char* description) {
    memory::InterruptGuard guard;
    bootPhase = phase;
    printf("BOOT "); printf(phase); printf(" "); printf(description); printf("\n");
}
static void Panic(const char *message, const char* code = "E00") {
    asm volatile("cli" ::: "memory");
    if (panicInProgress) {
        // Never recurse through a failing renderer, allocator or UART.
        const char marker[] = "PANIC RECURSIVE HALT\n";
        for (uint32_t i = 0; marker[i]; ++i)
            asm volatile("outb %0,$0xe9" :: "a"((uint8_t)marker[i]));
        for (;;) asm volatile("cli; hlt");
    }
    panicInProgress = true;
    printf("PANIC "); printf(code); printf(" PHASE "); printf(bootPhase); printf(" ");
    printf(message); printf("\n");
    bootLogEmergencyScreen();
    for (;;) asm volatile("cli; hlt");
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
static uint32_t DiskWaitTicks() { return activeTasks->Ticks(); }
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
static bool nativeFpDemo = false, nativeFpDiagnostic = false;
#if defined(GTOS_FP_DESKTOP_DIAGNOSTIC)
extern "C" bool native_fp_desktop_qualify_pointer_gap();
#endif
static bool StartNativeDemo(TaskManager &tasks, GlobalDescriptorTable &gdt,
                            const memory::MultibootInfo *boot) {
    // Enabled only by the dedicated acceptance ISO. Ordinary boots preserve
    // the existing ABI1 integer-only default and all FP-denial expectations.
    nativeFpDemo = BootOption(boot, "native-fp-test");
    nativeFpDiagnostic = BootOption(boot, "native-fp-diagnostic");
    if (nativeFpDiagnostic && !nativeFpDemo) return false;
#if !defined(GTOS_FP_DESKTOP_DIAGNOSTIC)
    if (nativeFpDiagnostic) {
        printf("NATIVE FP DIAGNOSTIC REQUIRES DEDICATED TEST KERNEL\n");
        return false;
    }
#endif
    if (nativeFpDemo) {
        uint32_t cr0; asm volatile("mov %%cr0,%0" : "=r"(cr0));
        LogValue("NATIVE FP INHERITED CR0 ", cr0); // Read-only firmware/cache diagnostic.
    }
    const process::NativeFpPolicy fpPolicy = nativeFpDemo ? process::NativeFpSse2 : process::NativeFpDisabled;
    if (!paging.sealForSharedProcessors() || !nativeRuntime.Activate(tasks, gdt, paging, frames, fpPolicy))
        return false;
#if defined(GTOS_FP_DESKTOP_DIAGNOSTIC)
    if (nativeFpDiagnostic) {
        memory::InterruptGuard guard;
        const process::NativeFpStatistics stats = nativeRuntime.FpStatistics();
        if (!nativeRuntime.FpEnabled() || stats.initialized || stats.saves || stats.restores
            || !native_fp_desktop_qualify_pointer_gap()) return false;
        printf("NATIVE FP DIRECT IF-CLEAR POINTER OMISSION QUALIFIED\n");
    }
#endif
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
    if (nativeFpDemo) printf("NATIVE FP DESKTOP TWO USERS READY\n");
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
    // The PNG consumer leaves an unfinished second draft on EXIT. Verify
    // Stop has already scrubbed it BEFORE Reap can supply a cleanup fallback.
    static gui::NativeImageSnapshot imageProof, imageAfter;
    process::NativeSurfaceBank& surfaces = process::NativeSurfaceBank::Instance();
    const bool imagePublished = surfaces.CopyLatest(0, imageProof);
    bool imageReclaimed = false;
    if (imagePublished) {
        const GtosSurfaceBeginRequest begin = {GTOS_SURFACE_ABI_VERSION, 1, 1, 4,
                                               GTOS_SURFACE_FORMAT_RGBA8_PREMULTIPLIED};
        const int handle = surfaces.Begin(nativeBrowserId, begin, sizeof(begin));
        GtosSurfaceControlRequest control = {GTOS_SURFACE_ABI_VERSION, (unsigned)handle};
        imageReclaimed = handle > 0 && (unsigned)handle > imageProof.generation
            && surfaces.Abort(nativeBrowserId, control, sizeof(control)) == 0;
    }
    uint32_t reaped = nativeRuntime.Reap();
    process::NativeStatistics counts = nativeRuntime.Statistics();
    valid = valid && reaped == 3 && counts.created == 3 && counts.faulted == 1 &&
            counts.exited == 2 && counts.reaped == 3 &&
            frames.getStatistics().freeFrames == nativeFrameBaseline;
    if (imagePublished) {
        imageReclaimed = imageReclaimed && surfaces.CopyLatest(0, imageAfter)
            && imageAfter.generation == imageProof.generation
            && imageAfter.width == imageProof.width && imageAfter.height == imageProof.height;
        for (uint32_t i = 0; i < gui::NativeImageMaximumBytes && imageReclaimed; ++i)
            imageReclaimed = imageAfter.rgba[i] == imageProof.rgba[i];
        valid = valid && imageReclaimed;
        printf(imageReclaimed ? "GTOS NATIVE SURFACE RECLAIM PASS V1\n"
                              : "GTOS NATIVE SURFACE RECLAIM FAIL V1\n");
    }
    if (nativeFpDemo) {
        process::NativeFpStatistics fp = nativeRuntime.FpStatistics();
        valid = valid && fp.saves > 100 && fp.restores == fp.saves && fp.initialized == 3
            && fp.invalidated == 3 && fp.invariantFailures == 0
            && fp.userKeyboardInterrupts && fp.userMouseInterrupts;
        LogValue("NATIVE FP CPL3 KEYBOARD IRQS ", fp.userKeyboardInterrupts);
        LogValue("NATIVE FP CPL3 MOUSE IRQS ", fp.userMouseInterrupts);
        if (nativeFpDiagnostic)
            printf(valid ? "NATIVE FP DESKTOP DIAGNOSTIC PASS (POINTER GAPS REMAIN)\n"
                         : "NATIVE FP DESKTOP DIAGNOSTIC FAIL\n");
        else
            printf(valid ? "NATIVE FP DESKTOP ISOLATION PASS\n" : "NATIVE FP DESKTOP ISOLATION FAIL\n");
    }
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
    common::BootLog::Reset();
    BootStage("B01", "HANDOFF");
    printf("GTOS 0.3 PROTECTED DESKTOP BOOT\n");
    BootStage("B02", "MEMORY");
    if (!frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end))
        Panic("INVALID MEMORY MAP", "E01");
    const memory::MultibootInfo *mbi = (const memory::MultibootInfo *)multiboot;
    const bool liveBoot = BootOption(mbi, "live");
    const bool uefiBoot = BootOption(mbi, "uefi");
    const bool diagnosticBoot = BootOption(mbi, "bootlog") || BootOption(mbi, "verbose");
    graphicsActive = uefiBoot;
    if (!BootOption(mbi, "legacy")) RegisterBootScreen(mbi, diagnosticBoot);
    if (BootOption(mbi, "serial")) {
        serialEnabled = StartBootSerial();
        if (serialEnabled) ReplayBootSerial();
        printf(serialEnabled ? "BOOT SERIAL COM1 115200 8N1\n" : "BOOT SERIAL COM1 UNAVAILABLE\n");
    }
    if (diagnosticBoot)
        printf(bootScreenReady ? "BOOT GOP DIAGNOSTIC READY\n"
                              : "BOOT GOP DIAGNOSTIC UNAVAILABLE - SERIAL OR EMULATOR E9 ONLY\n");
    if (uefiBoot) {
        graphicsActive = true; // EFI has no guaranteed legacy text framebuffer.
        printf("BOOT FIRMWARE UEFI X64\n");
    }
    if (liveBoot) {
        AdvancedTechnologyAttachment::DisableWritesForLiveBoot();
        printf("LIVE ATA WRITES DISABLED\n");
    }
    // The sole deliberate failure token is absent from normal delivery menus.
    // No storage initialization or runtime workload occurs on this test path.
    if (BootOption(mbi, "bootlog-fail")) Panic("SAFE DIAGNOSTIC FAILURE", "E99");
    memory::PhysicalMemoryStatistics physical = frames.getStatistics();
    LogValue("MEMORY FREE FRAMES ", physical.freeFrames);
    bool physicalOK = memory::RunPhysicalMemorySelfTest(frames);
    printf(physicalOK ? "PHYSICAL SELFTEST PASS\n" : "PHYSICAL SELFTEST FAIL\n");
    uint32_t heapAddress = 0;
    const uint32_t heapPages = 1024;
    if (!frames.allocateContiguous(heapPages, heapAddress))
        Panic("NO HEAP RAM", "E02");
    MemoryManager heap(heapAddress, heapPages * 4096);
    bool memoryOK = physicalOK && memory::RunHeapSelfTest() && heap.validate();
    printf(memoryOK ? "HEAP SELFTEST PASS\n" : "HEAP SELFTEST FAIL\n");
    if (!memoryOK)
        Panic("MEMORY SELFTEST", "E03");
    uint32_t ramMiB =
        (mbi->flags & 1) ? (mbi->memUpper + 2047) / 1024 : physical.addressableFrames / 256;
    BootStage("B03", "GDT IDT SCHEDULER");
    GlobalDescriptorTable gdt;
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
        Panic("SCHEDULER SELFTEST", "E04");
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    // Prepare resources before identity mappings are built. BSP GDT/IDT already
    // exist, while interrupts and paging remain disabled.
    CpuStartup cpuStartup;
    bool apsPrepared = cpuStartup.Prepare(cpu.GetInfo(), frames, ramMiB * 1024 * 1024);
    CpuWorkPool workers;
    bool workersPrepared = apsPrepared && workers.Prepare(cpuStartup, frames);
    CpuStartupReport apReport = cpuStartup.GetReport();
    BootStage("B04", liveBoot ? "RAM STORAGE" : "STORAGE");
    AdvancedTechnologyAttachment disk(0x1F0, true);
    // PIT below is 100 Hz. IF-clear boot uses finite polling; runtime gets 5 s.
    if (!disk.ConfigureWaitClock(DiskWaitTicks,500U)) Panic("ATA WAIT CLOCK", "E05");
    storage::BlockDevice *dataDisk = &disk;
    if (liveBoot) {
        // No Identify, read, or write is issued to any internal ATA disk.
        storage::LiveBlockDevice *ramDisk = new storage::LiveBlockDevice;
        if (!ramDisk) Panic("LIVE RAM STORAGE ALLOCATION", "E06");
        dataDisk = ramDisk;
    }
    storage::AppStore store(dataDisk);
    bool diskOK = store.Mount();
    if (liveBoot && !diskOK) Panic("LIVE RAM STORAGE FORMAT", "E07");
    printf(diskOK ? "APP STORE MOUNT OK\n" : "APP STORE UNAVAILABLE\n");
    if (liveBoot) {
        printf("LIVE RAM STORAGE READY\n");
        bool installed = false;
        if ((mbi->flags & (1 << 3)) && mbi->moduleCount) {
            const memory::MultibootModule *modules =
                (const memory::MultibootModule *)mbi->modules;
            if (modules[0].end > modules[0].start &&
                modules[0].end - modules[0].start <= apps::PackageLimit) {
                const uint8_t *package = (const uint8_t *)modules[0].start;
                const uint32_t length = modules[0].end - modules[0].start;
                if (apps::ValidatePackage(package, length) == apps::PackageOK) {
                    if (!store.Install(package, length)) Panic("LIVE BOOT PACKAGE INSTALL", "E08");
                    installed = true;
                }
            }
        }
        printf(installed ? "LIVE BOOT PACKAGE INSTALLED\n"
                         : "LIVE BOOT PACKAGE UNAVAILABLE\n");
    }
    LogValue("APP STORE COUNT ", store.Count());
    LogValue("APP STORE GENERATION ", store.Generation());
    storage::SettingsStore settings(dataDisk);
    bool settingsWritable = settings.Load();
    printf(liveBoot ? "SETTINGS RAM SESSION ONLY\n"
                    : (settingsWritable ? "SETTINGS READY\n" : "SETTINGS SESSION ONLY\n"));
    LogValue("SETTINGS LOCALE ", (uint32_t)settings.Current().locale);
    LogValue("SETTINGS THEME ", (uint32_t)settings.Current().theme);
    BootStage("B05", "GRAPHICS");
    gui::DesktopShell desktop(&store, liveBoot);
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
            modern = new gui::ModernDesktop(&framebuffer, &store, &settings, liveBoot,
                                            &process::NativeSurfaceBank::Instance());
        }
        if (!modern && backbuffer)
            frames.freeContiguous(backbuffer, backbufferPages);
    }
    process::NativeSurfaceBank::Instance().SetAvailable(modern != 0);
    if (uefiBoot && !modern) Panic("UEFI GOP FRAMEBUFFER UNAVAILABLE", "E09");
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
    BootDiagnosticKeyboard bootKeys(keyEvents, diagnosticBoot && bootScreenReady && modern);
    KeyboardDriver keyboard(&interrupts, &bootKeys);
    MouseDriver mouse(&interrupts, mouseEvents);
    VideoGraphicsArray vga;
    if (!modern && !vga.SetMode(320, 200, 8))
        Panic("VGA MODE", "E10");
    graphicsActive = true;
    BootStage("B06", "PAGING");
    memory::PagingDeviceRange devices[3] = {{0xA0000, 0x20000}, {0, 0}, {0, 0}};
    uint32_t deviceCount = 1;
    if (modern) {
        uint64_t begin = mbi->framebufferAddress & ~4095ULL;
        uint64_t end =
            (mbi->framebufferAddress + (uint64_t)mode.pitch * mode.height + 4095) & ~4095ULL;
        if (end > 0x100000000ULL || end <= begin || end - begin > 0xFFFFFFFFULL)
            Panic("FRAMEBUFFER MAPPING RANGE", "E11");
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
        Panic(memory::KernelPaging::ErrorName(paging.getLastError()), "E12");
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
        Panic(memory::KernelPaging::ErrorName(paging.getLastError()), "E13");
    uint32_t cr0;
    asm volatile("mov %%cr0,%0" : "=r"(cr0));
    memory::PagingMapping mapping;
    bool pagingOK = (cr0 & 0x80010000U) == 0x80010000U && !paging.query(0, mapping) &&
                    paging.query((uint32_t)&kernel_readonly_start, mapping) && !mapping.writable &&
                    paging.query(heapAddress, mapping) && mapping.writable;
    if (!pagingOK)
        Panic("PAGING PROTECTION SELFTEST", "E14");
    printf("PAGING PG WP NULL RO PASS\n");
    LogValue("PAGING CR3 ", paging.getStatistics().directoryAddress);
    Task sleeper(&gdt, Sleeper), yielder(&gdt, Yielder);
    tasks.AddTask(&sleeper);
    tasks.AddTask(&yielder);
    nativeDemoReady = nativeStacksPrepared && StartNativeDemo(tasks, gdt, mbi);
    if (!nativeDemoReady)
        printf("NATIVE RUNTIME LIMITED\n");
    BootStage("B07", "PS2 PIT IRQ");
    keyboard.Activate();
    mouse.Activate();
    // Explicit 100 Hz PIT, so VM/game timing is independent of loop throughput.
    Port8Bit pitControl(0x43), pitData(0x40);
    uint16_t divisor = 1193182 / 100;
    pitControl.Write(0x36);
    pitData.Write(divisor & 255);
    pitData.Write(divisor >> 8);
    interrupts.Activate();
    BootStage("B08", "DESKTOP");
    if (diagnosticBoot && bootScreenReady && modern) {
        printf("BOOT LOG PAUSED - PRESS ANY KEY FOR DESKTOP\n");
        while (bootKeys.Waiting()) asm volatile("sti; hlt");
        printf("BOOT LOG RESUME\n");
    }
    bootScreenEnabled = false;
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
