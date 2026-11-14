#define NativeProcessSmoke ContentIdentityUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "../apps/native_content_build/gtos-platform/probe/memory_record.h"

#if defined(GTOS_CONTENT_PLATFORM_LIBRARIES_PROBE)
#define CONTENT_BASE_PROBE "CONTENT PLATFORM LIBRARIES"
static const unsigned ExpectedChecks = 33;
static const char* const CheckDescription = "native CPU topology, ANGLE strings and Dawn sleep";
#elif defined(GTOS_CONTENT_V8_PLATFORM_PROBE)
#define CONTENT_BASE_PROBE "CONTENT V8 PLATFORM"
static const unsigned ExpectedChecks = 43;
static const char* const CheckDescription = "V8 native pages, TLS, clocks and concurrent once";
#elif defined(GTOS_CONTENT_FFMPEG_WEBM_PROBE)
#define CONTENT_BASE_PROBE "CONTENT FFMPEG WEBM"
#if defined(GTOS_CONTENT_FFMPEG_OPUS_PROBE)
static const unsigned ExpectedChecks = 46;
static const char* const CheckDescription = "WebM VP9 and Ogg Opus with native synchronization";
#else
static const unsigned ExpectedChecks = 32;
static const char* const CheckDescription = "WebM demux, VP9 frames and native synchronization";
#endif
#elif defined(GTOS_CONTENT_VP9_PROBE)
#define CONTENT_BASE_PROBE "CONTENT VP9"
static const unsigned ExpectedChecks = 20;
static const char* const CheckDescription = "VP9 frame decode and truncated packet";
#elif defined(GTOS_CONTENT_WEBRTC_THREAD_PROBE)
#define CONTENT_BASE_PROBE "CONTENT WEBRTC THREAD"
static const unsigned ExpectedChecks = 18;
static const char* const CheckDescription = "WebRTC worker, recursive mutex and realtime";
#elif defined(GTOS_CONTENT_EXPAT_PROBE)
#define CONTENT_BASE_PROBE "CONTENT EXPAT"
static const unsigned ExpectedChecks = 9;
static const char* const CheckDescription = "Expat parsing and hardware entropy requirement";
#elif defined(GTOS_CONTENT_ADDRESS_TEXT_PROBE)
#define CONTENT_BASE_PROBE "CONTENT ADDRESS TEXT"
static const unsigned ExpectedChecks = 23;
static const char* const CheckDescription = "address bytes and bounded text";
#elif defined(GTOS_CONTENT_NET_PROTOCOL_PROBE)
#define CONTENT_BASE_PROBE "CONTENT NET PROTOCOL"
static const unsigned ExpectedChecks = 16;
static const char* const CheckDescription = "cookie and DNS wire parsing";
#elif defined(GTOS_CONTENT_CAPACITY_PROBE)
#define CONTENT_BASE_PROBE "CONTENT CAPACITY"
static const unsigned ExpectedChecks = 136;
static const char* const CheckDescription = "Content image and resource resident budget";
#elif defined(GTOS_CONTENT_SKIA_SEMAPHORE_PROBE)
#define CONTENT_BASE_PROBE "CONTENT SKIA SEMAPHORE"
static const unsigned ExpectedChecks = 19;
static const char* const CheckDescription = "Skia wait, signal and native thread identity";
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
#if defined(GTOS_CONTENT_FFMPEG_OPUS_PROBE)
    static const unsigned MaximumImageBytes = 4 * 1024 * 1024;
    static const unsigned RecordAddress = 0x40300000U;
#elif defined(GTOS_CONTENT_VP9_PROBE) || defined(GTOS_CONTENT_FFMPEG_WEBM_PROBE) || \
    defined(GTOS_CONTENT_V8_PLATFORM_PROBE) || \
    defined(GTOS_CONTENT_PLATFORM_LIBRARIES_PROBE) || \
    defined(GTOS_CONTENT_WEBRTC_THREAD_PROBE) || \
    defined(GTOS_CONTENT_SKIA_SEMAPHORE_PROBE) || defined(GTOS_CONTENT_EXPAT_PROBE) || \
    defined(GTOS_CONTENT_ADDRESS_TEXT_PROBE) || defined(GTOS_CONTENT_NET_PROTOCOL_PROBE)
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
#if defined(GTOS_CONTENT_CAPACITY_PROBE)
    const NativeEnvironment environment;
    const NativeElfLaunchProfile ordinary{4096, 2, 1};
    const NativeElfLaunchProfile excessive{9000, 32, ProcessAddressSpace::MaximumContentCapacityPages};
    Need(!runtime.CreateElf(image, bytes, id, ordinary) && !id &&
        !runtime.CreateContentElf(image, bytes, id, excessive, environment) && !id &&
        frames.getStatistics().freeFrames == baseline, "failed admissions leave no allocated frames");
    const NativeElfLaunchProfile profile{9000, 32, 12288};
    Need(runtime.CreateContentElf(image, bytes, id, profile, environment), "Content ELF admission");
#elif defined(GTOS_CONTENT_V8_PLATFORM_PROBE)
    const NativeEnvironment environment;
    const NativeElfLaunchProfile profile{1024, 32, 1024};
    Need(runtime.CreateContentElf(image, bytes, id, profile, environment), "Content ELF admission");
#elif defined(GTOS_CONTENT_VP9_PROBE) || defined(GTOS_CONTENT_FFMPEG_WEBM_PROBE) || \
    defined(GTOS_CONTENT_PLATFORM_LIBRARIES_PROBE) || \
    defined(GTOS_CONTENT_WEBRTC_THREAD_PROBE) || \
    defined(GTOS_CONTENT_SKIA_SEMAPHORE_PROBE) || defined(GTOS_CONTENT_EXPAT_PROBE) || \
    defined(GTOS_CONTENT_ADDRESS_TEXT_PROBE) || defined(GTOS_CONTENT_NET_PROTOCOL_PROBE)
    const NativeElfLaunchProfile profile{1024, 32, 1024};
    Need(runtime.CreateElf(image, bytes, id, profile), "ELF admission");
#else
    Need(runtime.CreateElf(image, bytes, id), "ELF admission");
#endif
    const NativeStatus status = WaitStopped(runtime, tasks, id);
    gtos_perfetto_probe::Record record = {};
    Need(runtime.ReadMemory(id, RecordAddress, &record, sizeof(record)) &&
        record.version == 1 && record.mode == 0 && record.stage == 2, "completed consumer record");
#if defined(GTOS_CONTENT_PLATFORM_LIBRARIES_PROBE)
    struct {
        uint32_t failedLow, failedHigh, detectedL1dBytes, publishedL1dCount;
    } details = {};
    Need(runtime.ReadMemory(id, RecordAddress + sizeof(record), &details,
                            sizeof(details)), "consumer check mask");
    printf((char*)CONTENT_BASE_PROBE " MASK high="); printfHex32(details.failedHigh);
    printf((char*)" low="); printfHex32(details.failedLow);
    printf((char*)" l1d_bytes="); printfHex32(details.detectedL1dBytes);
    printf((char*)" l1d_count="); printfHex32(details.publishedL1dCount);
    printf((char*)"\n");
#endif
    Need(status.observedCs == 0x23 && status.observedCr3 == status.directory &&
        status.directory != paging.getStatistics().directoryAddress && !status.faultVector,
        "CPL3 isolation");
#if defined(GTOS_CONTENT_VP9_PROBE) || defined(GTOS_CONTENT_FFMPEG_WEBM_PROBE)
    printf((char*)CONTENT_BASE_PROBE " RECORD frame0="); printfHex32(record.base);
    printf((char*)" frame1="); printfHex32(record.fault_address);
    const bool values = record.base && record.fault_address && record.base != record.fault_address;
#elif defined(GTOS_CONTENT_EXPAT_PROBE)
    printf((char*)CONTENT_BASE_PROBE " RECORD entropy="); printfHex32(record.base);
    printf((char*)" callbacks="); printfHex32(record.fault_address);
    const bool values = record.base <= 1 && record.fault_address == (record.base ? 0x1243U : 0U);
#elif defined(GTOS_CONTENT_ADDRESS_TEXT_PROBE)
    printf((char*)CONTENT_BASE_PROBE " RECORD ipv4="); printfHex32(record.base);
    printf((char*)" mapped="); printfHex32(record.fault_address);
    const bool values = record.base == 0x7f000001U && record.fault_address == 0xc0000201U;
#elif defined(GTOS_CONTENT_NET_PROTOCOL_PROBE)
    printf((char*)CONTENT_BASE_PROBE " RECORD cookies="); printfHex32(record.base);
    printf((char*)" dns="); printfHex32(record.fault_address);
    const bool values = record.base == 3 && record.fault_address == 10;
#elif defined(GTOS_CONTENT_CAPACITY_PROBE)
    ProcessMemoryStatistics memory = {};
    Need(runtime.MemoryStatistics(id, memory), "Content resident memory statistics");
    printf((char*)CONTENT_BASE_PROBE " RECORD capacity="); printfHex32(record.base);
    printf((char*)" resources="); printfHex32(record.fault_address);
    const bool values = record.base == memory.capacityPages && record.fault_address == 12288 &&
        memory.dynamicPages == 12288 && memory.residentPages == memory.capacityPages &&
        memory.metadataFrames > 128;
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
