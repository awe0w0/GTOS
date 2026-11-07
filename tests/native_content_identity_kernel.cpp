#define NativeProcessSmoke ContentIdentityUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "../apps/native_content_build/gtos-platform/probe/memory_record.h"

#if defined(GTOS_CONTENT_VP9_PROBE)
#define CONTENT_BASE_PROBE "CONTENT VP9"
static const unsigned ExpectedChecks = 20;
static const char* const CheckDescription = "VP9 frame decode and truncated packet";
#elif defined(GTOS_CONTENT_WEBRTC_THREAD_PROBE)
#define CONTENT_BASE_PROBE "CONTENT WEBRTC THREAD"
static const unsigned ExpectedChecks = 18;
static const char* const CheckDescription = "WebRTC worker, recursive mutex and realtime";
#elif defined(GTOS_CONTENT_THREAD_PROBE)
#define CONTENT_BASE_PROBE "CONTENT NATIVE THREAD"
static const unsigned ExpectedChecks = 25;
static const char* const CheckDescription = "guarded thread start, join and detach";
#elif defined(GTOS_CONTENT_TLS_PROBE)
#define CONTENT_BASE_PROBE "CONTENT BASE TLS"
static const unsigned ExpectedChecks = 28;
static const char* const CheckDescription = "isolated values and exit callbacks";
#else
#define CONTENT_BASE_PROBE "CONTENT BASE IDENTITY"
static const unsigned ExpectedChecks = 23;
static const char* const CheckDescription = "exact process and worker identity";
#endif

namespace {
#if defined(GTOS_CONTENT_VP9_PROBE) || defined(GTOS_CONTENT_WEBRTC_THREAD_PROBE)
    static const unsigned MaximumImageBytes = 2 * 1024 * 1024;
    static const unsigned RecordAddress = 0x40200000U;
#else
    static const unsigned MaximumImageBytes = 65536;
    static const unsigned RecordAddress = gtos_perfetto_probe::RecordAddress;
#endif
    uint8_t image[MaximumImageBytes];
    void FinishIdentity(bool pass) __attribute__((noreturn));
    void FinishIdentity(bool pass) {
        printf(pass ? (char*)CONTENT_BASE_PROBE " PASS\n" : (char*)CONTENT_BASE_PROBE " FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void Need(bool value, const char* reason) {
        if (!value) {
            printf((char*)CONTENT_BASE_PROBE " REJECT ");
            printf((char*)reason);
            printf((char*)"\n");
            FinishIdentity(false);
        }
    }
}

extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    Need(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        (const MultibootInfo*)multiboot, 0, 0};
    Need(paging.prepareIdentity(frames, config), "identity paging");
    NativeRuntime runtime;
    Need(runtime.PrepareStacks(paging, frames), "guarded stacks");
    Need(paging.enable() && paging.sealForSharedProcessors(), "sealed paging template");
    Need(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2, true), "bounded native threads");
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    Need(tasks.AddTask(&ring0), "kernel peer");
    const auto* boot = static_cast<const MultibootInfo*>(multiboot);
    Need((boot->flags & 8) && boot->moduleCount == 1, "one supplied ELF");
    const auto& module = *reinterpret_cast<const MultibootModule*>(boot->modules);
    Need(module.end > module.start && module.end - module.start <= sizeof(image), "bounded ELF");
    const uint32_t bytes = module.end - module.start;
    for (uint32_t i = 0; i < bytes; ++i) image[i] = ((const uint8_t*)module.start)[i];
    interrupts.Activate();
    uint32_t id = 0;
#if defined(GTOS_CONTENT_VP9_PROBE) || defined(GTOS_CONTENT_WEBRTC_THREAD_PROBE)
    const NativeElfLaunchProfile profile{1024, 32, 1024};
    Need(runtime.CreateElf(image, bytes, id, profile), "ELF admission");
#else
    Need(runtime.CreateElf(image, bytes, id), "ELF admission");
#endif
    const NativeStatus status = WaitStopped(runtime, tasks, id);
    gtos_perfetto_probe::Record record = {};
    Need(runtime.ReadMemory(id, RecordAddress, &record, sizeof(record)) &&
        record.version == 1 && record.mode == 0 && record.stage == 2, "completed consumer record");
    Need(status.observedCs == 0x23 && status.observedCr3 == status.directory &&
        status.directory != paging.getStatistics().directoryAddress && !status.faultVector,
        "CPL3 isolation");
#if defined(GTOS_CONTENT_VP9_PROBE)
    printf((char*)CONTENT_BASE_PROBE " RECORD frame0="); printfHex32(record.base);
    printf((char*)" frame1="); printfHex32(record.fault_address);
    const bool values = record.base && record.fault_address && record.base != record.fault_address;
#else
    printf((char*)CONTENT_BASE_PROBE " RECORD process="); printfHex32(record.base);
    printf((char*)" worker="); printfHex32(record.fault_address);
    const bool values = record.base == id && record.fault_address > id;
#endif
    printf((char*)" checks="); printfHex32(record.checks);
    printf((char*)" failures="); printfHex32(record.failures);
    printf((char*)" exit="); printfHex32(status.exitCode); printf((char*)"\n");
    Need(values && record.checks == ExpectedChecks && !record.failures && status.exitCode == 0,
        CheckDescription);
    Need(runtime.Reap() == 1 && frames.getStatistics().freeFrames == baseline, "exact Reap frames");
    printf((char*)CONTENT_BASE_PROBE " REAP free="); printfHex32(frames.getStatistics().freeFrames);
    printf((char*)" expected="); printfHex32(baseline); printf((char*)"\n");
    FinishIdentity(true);
}
